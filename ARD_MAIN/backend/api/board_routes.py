"""Small settings API, independent of the camera and AI configuration API."""
from flask import jsonify, request


def register_board_routes(app, settings, on_change):
    @app.route('/api/hardware/config', methods=['GET', 'POST'])
    def hardware_config():
        if request.method == 'GET':
            return jsonify({"status": "success", "hardware": settings.snapshot()})
        before = settings.snapshot()
        try:
            after = settings.update(request.get_json(silent=True))
        except (ValueError, TypeError) as exc:
            return jsonify({"status": "error", "message": str(exc)}), 400
        except OSError:
            return jsonify({"status": "error", "message": "보드 설정 파일을 저장하지 못했습니다."}), 500
        on_change(before, after)
        return jsonify({"status": "success", "hardware": after})
