/**
 * ARD Control Center - Auto Font Resizer (Text Shrink to Fit)
 * 컨테이너에서 글자가 넘칠 때 폰트 크기를 오차 없이 안전하게 축소합니다.
 * DOM 크기 복원-감소 반복으로 인한 깜빡임(Flicker)을 원천 차단하기 위해
 * Offscreen Canvas 기반의 수학적 1회 계산(Zero-Flicker Calculation) 및
 * 내용/너비 기준 캐싱(fittedKey)을 적용합니다.
 */

(function () {
    'use strict';

    // 텍스트 측정을 위한 오프스크린 캔버스 인스턴스
    const measureCanvas = document.createElement('canvas');
    const measureCtx = measureCanvas.getContext('2d');

    let isAdjusting = false;

    /**
     * 대상 요소들의 텍스트 크기를 부모/자신 컨테이너에 맞춤
     * @param {HTMLElement|Document} root 
     */
    function autoFitText(root = document) {
        if (isAdjusting) return;
        isAdjusting = true;

        try {
            const selector = [
                '.btn-primary',
                '.tab-btn',
                '.subtab-btn',
                '.sandwich-btn',
                '.status-label',
                '.status-value',
                '.meta-row',
                '.server-ip-line',
                '.server-gps-line',
                '.hud-label',
                '.hud-value',
                '.hud-msg',
                '.queue-item .cmd-name',
                '.result-item label',
                '.result-item span',
                '.pricing-table th',
                '.pricing-table td'
            ].join(', ');

            const elements = root.querySelectorAll ? root.querySelectorAll(selector) : [];

            elements.forEach(el => {
                // 비가시 요소(숨김 탭/모달 등)는 레이아웃 치수가 0이므로 건너뜀
                if (!el.offsetParent && !el.classList.contains('active')) {
                    return;
                }

                // 원본 폰트 크기 초기 1회 영구 보존
                if (!el.dataset.origFontSize) {
                    const computed = window.getComputedStyle(el);
                    const currentSize = parseFloat(computed.fontSize);
                    if (!currentSize || isNaN(currentSize)) return;
                    el.dataset.origFontSize = currentSize;
                    if (computed.fontFamily) el.dataset.origFontFamily = computed.fontFamily;
                    if (computed.fontWeight) el.dataset.origFontWeight = computed.fontWeight;
                }

                const origSize = parseFloat(el.dataset.origFontSize);
                if (!origSize || isNaN(origSize)) return;

                // On phones, wrap control labels instead of shrinking them.
                if (window.matchMedia('(max-width: 1100px)').matches &&
                    el.matches('.btn-primary, .tab-btn, .subtab-btn, .sandwich-btn')) {
                    el.style.fontSize = `${Math.max(origSize, 14)}px`;
                    el.style.whiteSpace = 'normal';
                    delete el.dataset.fittedKey;
                    return;
                }

                const text = el.textContent ? el.textContent.trim() : '';
                const clientWidth = el.clientWidth;
                if (!clientWidth) return;

                // 캐싱 키: 텍스트 내용과 컨테이너 너비가 일치하면 재계산 절대 생략 (진동/깜빡임 100% 방지)
                const cacheKey = `${text}_${clientWidth}`;
                if (el.dataset.fittedKey === cacheKey) {
                    return;
                }

                const computed = window.getComputedStyle(el);
                const padLeft = parseFloat(computed.paddingLeft) || 0;
                const padRight = parseFloat(computed.paddingRight) || 0;
                
                // 자식 요소(SVG 아이콘 등)가 차지하는 너비 계산
                let innerIconWidth = 0;
                const svgs = el.querySelectorAll('svg');
                svgs.forEach(svg => {
                    innerIconWidth += (svg.getBoundingClientRect().width || 18) + 6;
                });

                const availableWidth = Math.max(10, clientWidth - padLeft - padRight - innerIconWidth - 4);
                const fontFamily = el.dataset.origFontFamily || computed.fontFamily || 'Outfit, sans-serif';
                const fontWeight = el.dataset.origFontWeight || computed.fontWeight || 'normal';

                // Canvas를 통해 원본 폰트 크기 상태에서의 텍스트 폭을 1회 측정
                measureCtx.font = `${fontWeight} ${origSize}px ${fontFamily}`;
                const textMetrics = measureCtx.measureText(text);
                const measuredWidth = textMetrics.width;

                const minSize = Math.max(origSize * 0.65, 9); // 최소 크기 보장 (가독성 유지)

                if (measuredWidth > availableWidth && availableWidth > 0) {
                    // 비율 계산을 통해 필요한 최적 폰트 크기를 즉시 산출 (DOM 루프 제거)
                    const targetRatio = availableWidth / measuredWidth;
                    let targetSize = Math.floor(origSize * targetRatio * 10) / 10;
                    if (targetSize < minSize) targetSize = minSize;

                    el.style.fontSize = `${targetSize}px`;
                    el.style.whiteSpace = 'nowrap';
                    el.dataset.isShrunk = 'true';
                } else {
                    // 여유 공간이 충분한 경우 원본 크기로 설정
                    if (el.style.fontSize && el.style.fontSize !== `${origSize}px`) {
                        el.style.fontSize = `${origSize}px`;
                    }
                    el.dataset.isShrunk = 'false';
                }

                el.dataset.fittedKey = cacheKey;
            });
        } finally {
            // 미세 렌더링 틱 후 잠금 해제
            setTimeout(() => {
                isAdjusting = false;
            }, 30);
        }
    }

    // 디바운스 유틸리티
    function debounce(func, wait) {
        let timeout;
        return function (...args) {
            clearTimeout(timeout);
            timeout = setTimeout(() => func.apply(this, args), wait);
        };
    }

    const debouncedFit = debounce(() => autoFitText(document), 100);

    // 1. 초기 로드 시 안전 실행
    if (document.readyState === 'loading') {
        document.addEventListener('DOMContentLoaded', () => {
            setTimeout(autoFitText, 100);
        });
    } else {
        setTimeout(autoFitText, 100);
    }

    // 2. 창 크기 변경 시 디바운스 실행
    window.addEventListener('resize', debouncedFit);

    // 3. 전역 함수 노출
    window.autoFitText = autoFitText;

    // 4. 텍스트 내용 변경 감지 (스타일 변경은 무시하여 무한 루프 차단)
    const observer = new MutationObserver((mutations) => {
        if (isAdjusting) return;
        const hasContentChange = mutations.some(m => 
            m.type === 'childList' || 
            m.type === 'characterData'
        );
        if (hasContentChange) {
            debouncedFit();
        }
    });

    const initObserver = () => {
        if (document.body) {
            observer.observe(document.body, {
                childList: true,
                characterData: true,
                subtree: true
            });
        }
    };

    if (document.body) {
        initObserver();
    } else {
        document.addEventListener('DOMContentLoaded', initObserver);
    }
})();
