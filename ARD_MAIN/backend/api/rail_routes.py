"""Z rail measurement and leveling API (drive tab tools). See core/rail_control.py."""
from flask import jsonify, request


def register_rail_routes(app, tools, get_ip, sorting_active, allowed_terminal=None, cancel_manual=None,
                         get_ratios=lambda: (1.0, 1.0)):
    def board_ip():
        ip = get_ip()
        if not ip or ip == 'Unknown':
            raise ConnectionError('DriveMCU IP가 없습니다. 보드 연결을 확인하세요.')
        return ip

    def call(fn, *args, tool=True):
        try:
            if tool and sorting_active():
                return jsonify({'status': 'busy', 'message': '선별 작업 중에는 레일 도구를 사용할 수 없습니다.'}), 409
            ip = board_ip()
            result = tools.run(fn, ip, *args) if tool else fn(ip, *args)
            return jsonify({'status': 'success', 'result': result, 'config': tools.snapshot()})
        except BlockingIOError as exc:
            return jsonify({'status': 'busy', 'message': str(exc)}), 409
        except ValueError as exc:
            return jsonify({'status': 'error', 'message': str(exc)}), 400
        except (ConnectionError, OSError) as exc:
            return jsonify({'status': 'error', 'message': f'DriveMCU 통신 실패: {exc}'}), 503
        except RuntimeError as exc:
            return jsonify({'status': 'error', 'message': str(exc), 'config': tools.snapshot()}), 409

    def body():
        data = request.get_json(silent=True)
        return data if isinstance(data, dict) else {}

    @app.route('/api/rail/config', methods=['GET', 'POST'])
    def rail_config():
        if request.method == 'GET':
            return jsonify({'status': 'success', 'config': tools.snapshot(), 'busy': tools.busy})
        data = body()
        apply = bool(data.pop('apply', False))
        try:
            config = tools.update(data)
        except (ValueError, TypeError) as exc:
            return jsonify({'status': 'error', 'message': str(exc)}), 400
        except OSError:
            return jsonify({'status': 'error', 'message': '레일 설정 파일을 저장하지 못했습니다.'}), 500
        if not apply:
            return jsonify({'status': 'success', 'config': config})
        return call(lambda ip: tools.apply(ip), tool=False)

    @app.route('/api/rail/apply', methods=['POST'])
    def rail_apply():
        return call(lambda ip: tools.apply(ip), tool=False)

    @app.route('/api/rail/live', methods=['GET'])
    def rail_live():
        return call(lambda ip: tools.live(ip), tool=False)

    @app.route('/api/rail/pid', methods=['POST'])
    def rail_pid():
        return call(tools.apply_pid, body())

    @app.route('/api/jc1060/rail', methods=['POST'])
    def terminal_rail():
        if allowed_terminal is None or not allowed_terminal():
            return jsonify({'status': 'inactive'}), 403
        if sorting_active():
            return jsonify({'status': 'busy', 'message': '선별 작업 중에는 수동 조작할 수 없습니다.'}), 409
        data = body()
        if cancel_manual and cancel_manual() and data.get('op') != 'stop':
            return jsonify({'status': 'busy', 'message': '조그를 정지했습니다. 정지 확인 후 다시 조작하세요.'}), 409
        return call(tools.terminal_control, data)

    @app.route('/api/rail/tof-sample', methods=['POST'])
    def rail_tof_sample():
        seconds = body().get('seconds', 3)
        if isinstance(seconds, bool) or not isinstance(seconds, (int, float)) or not 1 <= seconds <= 30:
            return jsonify({'status': 'error', 'message': '측정 시간은 1~30초여야 합니다.'}), 400
        return call(tools.sample_tof, float(seconds))

    @app.route('/api/rail/measure-direction', methods=['POST'])
    def rail_measure_direction():
        data = body()
        steps = data.get('steps', 400)
        if isinstance(steps, bool) or not isinstance(steps, int):
            return jsonify({'status': 'error', 'message': '측정 스텝은 정수여야 합니다.'}), 400
        return call(tools.measure_direction, steps, bool(data.get('apply', False)))

    @app.route('/api/rail/teach', methods=['POST'])
    def rail_teach():
        return call(tools.teach, body().get('slot'))

    @app.route('/api/rail/goto', methods=['POST'])
    def rail_goto():
        data = body()
        try:
            target = tools.target_of(slot=data.get('slot'), mm=data.get('mm')) if 'slot' in data \
                else tools.target_of(mm=data.get('mm'))
        except ValueError as exc:
            return jsonify({'status': 'error', 'message': str(exc)}), 400
        return call(tools.goto, target, bool(data.get('hold', True)))

    @app.route('/api/rail/sweep', methods=['POST'])
    def rail_sweep():
        cycles = body().get('cycles', 3)
        if isinstance(cycles, bool) or not isinstance(cycles, int):
            return jsonify({'status': 'error', 'message': '반복 횟수는 정수여야 합니다.'}), 400
        return call(tools.sweep, cycles)

    @app.route('/api/rail/test-drop', methods=['POST'])
    def rail_test_drop():
        """배출 위치 시험: one material's rail target and tilt, no camera or AI."""
        return call(tools.test_drop, body().get('category'), get_ratios())

    @app.route('/api/rail/cancel', methods=['POST'])
    def rail_cancel():
        tools.cancel()
        return jsonify({'status': 'success', 'busy': tools.busy})
