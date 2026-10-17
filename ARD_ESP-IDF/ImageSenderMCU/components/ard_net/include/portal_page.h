#pragma once

static const char ARD_PORTAL_PAGE[] =
"<!doctype html><html lang='ko'><meta charset='utf-8'>"
"<meta name='viewport' content='width=device-width,initial-scale=1'>"
"<title>ARD 네트워크 설정</title><style>"
"body{font:16px system-ui;background:#f1f5f9;color:#172033;margin:24px auto;padding:16px;max-width:520px}"
"main{background:white;padding:24px;border-radius:16px}input,button{box-sizing:border-box;width:100%;padding:12px;margin:8px 0 18px;font:inherit}"
"button{background:#176b51;color:white;border:0;border-radius:8px}small{color:#526174}"
"</style><main><h1>네트워크 설정</h1><p>{{HOST}}.local</p><p>{{STATUS}}</p>"
"<form method='post' action='/network/save'>"
"<input type='hidden' name='token' value='{{TOKEN}}'>"
"<label>Wi-Fi 이름 (2.4GHz)<input name='ssid' value='{{SSID}}' required maxlength='32' autocomplete='off'></label>"
"<label>Wi-Fi 비밀번호<input type='password' name='password' maxlength='64' autocomplete='new-password'></label>"
"<small>저장된 비밀번호는 표시하지 않습니다. 저장할 때 다시 입력하세요. 공개 Wi-Fi는 비워 두세요.</small>"
"<p><label>PC 서버 IPv4 또는 호스트 이름<input name='server' value='{{SERVER}}' required maxlength='63' placeholder='192.168.0.35 또는 pc-name.local'></label></p>"
"<small>http://와 포트 없이 입력하세요. PC 서비스 포트는 5000/5001을 사용합니다.</small>"
"<button type='submit'>저장 후 재시작</button></form>"
"<p><a href='/network'>상태 새로고침</a></p><small>저장하면 보드가 재시작됩니다. 장치를 정지시킨 뒤 설정하세요."
" 연결에 실패해도 설정 AP로 다시 접속할 수 있습니다.</small></main></html>";
