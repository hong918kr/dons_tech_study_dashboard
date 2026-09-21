# 🖥️ 대시보드 로컬 서버 (항상 켜져 있음)

대시보드를 **항상 접속 가능한 고정 주소**로 띄우는 자동 서버 설정. macOS `launchd` LaunchAgent 사용 —
**로그인 시 자동 시작 + 프로세스가 죽으면 자동 재시작** (검증: 강제 kill 후 1초 만에 부활).

## 📌 접속 주소 (이걸 북마크)
```
http://localhost:8787/index.html                     ← 대시보드
http://localhost:8787/resume/DonHong_Resume_Anduril.html  ← 레주메
```
> ⚠️ 예전 `:8765` 는 임시 서버였음(그래서 죽었음). 앞으로는 **`:8787`** 만 쓰세요.

## 대안: 서버 없이 열기
서버가 필요 없으면 파일을 그냥 열어도 100% 동작(오프라인, 자체완결형):
```sh
open /Users/donh/workspace/dons_tech_study_dashboard/internview_prep/anduril_firmware_engineer/index.html
```
> 단, `file://` 로 연 것과 `http://localhost:8787` 은 진행률(localStorage) 저장소가 **분리**됨. 한쪽으로 통일해서 쓰는 걸 권장 (추천: localhost:8787).

---

## 관리 명령어

| 하고 싶은 것 | 명령어 |
|---|---|
| 상태 확인 | `launchctl list \| grep andurilprep` |
| 지금 재시작 | `launchctl kickstart -k gui/$(id -u)/com.don.andurilprep` |
| 잠깐 끄기 | `launchctl unload ~/Library/LaunchAgents/com.don.andurilprep.plist` |
| 다시 켜기 | `launchctl load -w ~/Library/LaunchAgents/com.don.andurilprep.plist` |
| 로그 보기 | `cat /tmp/anduril_prep_serve.log` |
| 완전 제거 | `launchctl unload ~/Library/LaunchAgents/com.don.andurilprep.plist && rm ~/Library/LaunchAgents/com.don.andurilprep.plist` |

## 세부 사항
- **설정 파일**: `~/Library/LaunchAgents/com.don.andurilprep.plist`
- **서버**: `python3 -m http.server 8787 --bind 127.0.0.1` (localhost 전용, 외부 노출 안 함)
- **서빙 폴더**: 이 프로젝트 폴더 (`WorkingDirectory`)
- **포트**: 8787 (다른 것과 안 겹침)
- 데이터(`data/*.json`) 수정 후엔 `python3 build_dashboard.py` 로 `index.html` 재생성 → 브라우저 새로고침이면 반영.
