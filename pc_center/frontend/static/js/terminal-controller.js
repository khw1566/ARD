/**
 * ARD Internal Web Terminal Controller
 * Manages modal display, command history, execution via /api/terminal/execute, and output rendering.
 */

(function () {
    'use strict';

    let cmdHistory = [];
    let historyIndex = -1;

    function initTerminal() {
        const modalBtn = document.getElementById('terminal-modal-btn');
        const modal = document.getElementById('terminal-modal');
        const closeBtn = document.getElementById('terminal-modal-close');
        const input = document.getElementById('terminal-input');
        const sendBtn = document.getElementById('terminal-send-btn');
        const screen = document.getElementById('terminal-screen');
        const quickBtns = document.querySelectorAll('.term-quick-btn');

        // Open Modal (if modal and modal button exist on page)
        if (modalBtn && modal) {
            modalBtn.addEventListener('click', () => {
                modal.classList.remove('hidden');
                if (input) {
                    setTimeout(() => input.focus(), 50);
                }
            });
        }

        // Close Modal
        if (closeBtn && modal) {
            closeBtn.addEventListener('click', () => {
                modal.classList.add('hidden');
            });
        }

        // Close on background click
        if (modal) {
            modal.addEventListener('click', (e) => {
                if (e.target === modal) {
                    modal.classList.add('hidden');
                }
            });
        }

        // Quick Command Buttons
        quickBtns.forEach((btn) => {
            btn.addEventListener('click', () => {
                const cmd = btn.getAttribute('data-cmd');
                if (cmd) {
                    if (input) input.value = cmd;
                    handleCommand(cmd);
                }
            });
        });

        // Send Button Click
        if (sendBtn) {
            sendBtn.addEventListener('click', () => {
                if (input) {
                    const cmd = input.value.trim();
                    if (cmd) {
                        handleCommand(cmd);
                        input.value = '';
                    }
                }
            });
        }

        // Input Key Handling (Enter, Arrow Up, Arrow Down)
        if (input) {
            input.addEventListener('keydown', (e) => {
                if (e.key === 'Enter') {
                    e.preventDefault();
                    const cmd = input.value.trim();
                    if (cmd) {
                        handleCommand(cmd);
                        input.value = '';
                    }
                } else if (e.key === 'ArrowUp') {
                    e.preventDefault();
                    if (cmdHistory.length > 0) {
                        if (historyIndex === -1) {
                            historyIndex = cmdHistory.length - 1;
                        } else if (historyIndex > 0) {
                            historyIndex--;
                        }
                        input.value = cmdHistory[historyIndex] || '';
                    }
                } else if (e.key === 'ArrowDown') {
                    e.preventDefault();
                    if (historyIndex !== -1) {
                        if (historyIndex < cmdHistory.length - 1) {
                            historyIndex++;
                            input.value = cmdHistory[historyIndex] || '';
                        } else {
                            historyIndex = -1;
                            input.value = '';
                        }
                    }
                }
            });
        }

        async function handleCommand(cmd) {
            // Add to history
            if (cmd && (cmdHistory.length === 0 || cmdHistory[cmdHistory.length - 1] !== cmd)) {
                cmdHistory.push(cmd);
            }
            historyIndex = -1;

            // Render command line
            appendLine(cmd, 'term-cmd-in');

            // Handle client-side clear
            if (cmd.toLowerCase() === 'clear') {
                if (screen) {
                    screen.innerHTML = `
                        <div class="term-line term-welcome">ARD Interactive System Terminal [Version 2.4.0]</div>
                        <div class="term-line term-welcome">'help'를 입력하면 사용 가능한 내부 명령어를 확인할 수 있습니다.</div>
                        <div class="term-line" style="margin-bottom: 8px;">---------------------------------------------------------------------</div>
                    `;
                }
                return;
            }

            // Execute API
            try {
                const response = await fetch('/api/terminal/execute', {
                    method: 'POST',
                    headers: { 'Content-Type': 'application/json' },
                    body: JSON.stringify({ command: cmd })
                });

                if (!response.ok) {
                    appendLine(`[HTTP 오류] 서버 통신 실패: ${response.status}`, 'term-cmd-err');
                    return;
                }

                const result = await response.json();
                if (result.output === '__CLEAR__') {
                    if (screen) screen.innerHTML = '';
                    return;
                }

                const cls = result.status === 'error' ? 'term-cmd-err' : (result.status === 'restarting' || result.status === 'stopping') ? 'term-cmd-warn' : 'term-cmd-out';
                appendLine(result.output, cls);

                if (result.status === 'restarting') {
                    appendLine('[안내] 4초 후 웹페이지를 자동으로 새로고침합니다...', 'term-cmd-warn');
                    setTimeout(() => {
                        window.location.reload();
                    }, 4000);
                }
            } catch (err) {
                appendLine(`[네트워크 에러] 서버에 접근할 수 없습니다: ${err.message}`, 'term-cmd-err');
            }
        }

        function appendLine(text, className) {
            if (!screen) return;
            const div = document.createElement('div');
            div.className = `term-line ${className || ''}`;
            div.textContent = text;
            screen.appendChild(div);
            screen.scrollTop = screen.scrollHeight;
        }
    }

    if (document.readyState === 'loading') {
        document.addEventListener('DOMContentLoaded', initTerminal);
    } else {
        initTerminal();
    }
})();
