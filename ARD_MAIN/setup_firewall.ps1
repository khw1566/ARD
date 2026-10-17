# 방화벽 설정 스크립트 (관리자 권한 필요)
# TCP 5000 (웹 UI/API), UDP 5001 (이미지 스트리밍) 포트를 개방합니다.

$RuleName1 = "ARD_Control_Center_TCP"
$RuleName2 = "ARD_Control_Center_UDP"

# 기존 규칙 삭제 (중복 방지)
Remove-NetFirewallRule -DisplayName $RuleName1 -ErrorAction SilentlyContinue
Remove-NetFirewallRule -DisplayName $RuleName2 -ErrorAction SilentlyContinue

write-host "신규 방화벽 규칙을 추가하는 중..." -ForegroundColor Cyan

# TCP 5000 포트 추가
New-NetFirewallRule -DisplayName $RuleName1 -Direction Inbound -LocalPort 5000 -Protocol TCP -Action Allow -Description "ARD Control Center Web/API Port"

# UDP 5001 포트 추가
New-NetFirewallRule -DisplayName $RuleName2 -Direction Inbound -LocalPort 5001 -Protocol UDP -Action Allow -Description "ARD Control Center Streaming Port"

write-host "`n[성공] 방화벽 설정이 완료되었습니다!" -ForegroundColor Green
write-host "이제 방화벽을 켠 상태에서도 송신기와 통신이 가능합니다."
write-host "Enter를 눌러 창을 닫으세요..."
Read-Host
