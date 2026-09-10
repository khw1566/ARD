/**
 * ARD Control Center - Auto Font Resizer (Text Shrink to Fit)
 * 엑셀의 '텍스트 줄 바꿈 방지 및 셀 크기에 맞춤'과 동일하게
 * 컨테이너(버튼, 상태 라벨, 헤더 등)에서 글자가 넘치면(overflow)
 * 줄바꿈 대신 폰트 크기를 단계적으로 축소하여 영역 내에 꼭 맞춥니다.
 */

(function () {
    'use strict';

    /**
     * 대상 요소들의 텍스트 크기를 부모/자신 컨테이너에 맞춤
     * @param {HTMLElement|Document} root 
     */
    function autoFitText(root = document) {
        // 자동 텍스트 크기 맞춤 대상 셀렉터
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
            // 다중 행 텍스트 영역이나 숨겨진 요소는 스킵
            if (el.offsetParent === null && !el.classList.contains('active')) {
                // 현재 보이지 않는 탭/모달 등은 활성화될 때 재측정되도록 보존
                return;
            }

            // 원본 폰트 크기 기록 (초기 1회)
            if (!el.dataset.origFontSize) {
                const computed = window.getComputedStyle(el);
                const currentSize = parseFloat(computed.fontSize);
                if (!currentSize || isNaN(currentSize)) return;
                el.dataset.origFontSize = currentSize;
            }

            const origSize = parseFloat(el.dataset.origFontSize);
            const minSize = Math.max(origSize * 0.6, 9); // 최소 60% 또는 9px 이하로는 축소 방지 (가독성 유지)

            // 먼저 원래 크기로 복원하여 넘침 여부 재측정
            el.style.fontSize = `${origSize}px`;

            // 가로 또는 세로 overflow 확인
            // scrollWidth > clientWidth 이면 텍스트가 가로로 넘치는 상태
            let currentSize = origSize;
            let iterations = 0;

            // scrollWidth가 clientWidth보다 크거나 scrollHeight가 clientHeight보다 큰 경우 축소 반복
            while (
                (el.scrollWidth > el.clientWidth + 1 || el.scrollHeight > el.clientHeight + 1) &&
                currentSize > minSize &&
                iterations < 15
            ) {
                currentSize -= 0.5;
                el.style.fontSize = `${currentSize}px`;
                iterations++;
            }
        });
    }

    // 디바운스 유틸리티
    function debounce(func, wait) {
        let timeout;
        return function (...args) {
            clearTimeout(timeout);
            timeout = setTimeout(() => func.apply(this, args), wait);
        };
    }

    const debouncedFit = debounce(() => autoFitText(document), 60);

    // 1. DOM 로드 완료 시 실행
    if (document.readyState === 'loading') {
        document.addEventListener('DOMContentLoaded', () => {
            autoFitText();
            setTimeout(autoFitText, 200);
            setTimeout(autoFitText, 800);
        });
    } else {
        autoFitText();
        setTimeout(autoFitText, 200);
        setTimeout(autoFitText, 800);
    }

    // 2. 창 크기 변경 및 리사이저 조작 시 자동 크기 맞춤
    window.addEventListener('resize', debouncedFit);

    // 3. 전역 노출 (언어 전환, 탭 전환, 동적 상태 갱신 시 즉시 호출 가능)
    window.autoFitText = autoFitText;

    // 4. MutationObserver를 통해 언어나 동적 텍스트 내용 변경 감지 시 자동 실행
    let isAdjusting = false;
    const observer = new MutationObserver((mutations) => {
        if (isAdjusting) return;
        const hasContentChange = mutations.some(m => 
            m.type === 'childList' || 
            m.type === 'characterData' ||
            (m.type === 'attributes' && m.attributeName === 'class')
        );
        if (hasContentChange) {
            isAdjusting = true;
            debouncedFit();
            setTimeout(() => { isAdjusting = false; }, 100);
        }
    });

    if (document.body) {
        observer.observe(document.body, {
            childList: true,
            characterData: true,
            subtree: true,
            attributes: true,
            attributeFilter: ['class']
        });
    } else {
        document.addEventListener('DOMContentLoaded', () => {
            observer.observe(document.body, {
                childList: true,
                characterData: true,
                subtree: true,
                attributes: true,
                attributeFilter: ['class']
            });
        });
    }
})();
