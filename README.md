# Local Conversational AI

메타휴먼과 한국어로 대화하는 프로그램이다. 사용자가 텍스트를 입력하면 로컬 LLM이
답하고, 로컬 TTS가 음성을 만들고, 메타휴먼이 그 음성에 맞춰 입을 움직인다.

외부 API를 쓰지 않는다. 전부 한 대의 PC에서 돈다.

처음 보는 사람은 [docs/프로젝트 구조.md](docs/프로젝트%20구조.md)부터 읽는다.
동작 원리는 [docs/동작원리.md](docs/동작원리.md) 참고.

연구·내부 데모 목적으로만 쓴다. (XTTS-v2가 비상업 라이선스 CPML)

## 구성

| 경로 | 내용 |
|---|---|
| `server/` | FastAPI 서버. LLM · TTS · 강제 정렬 · viseme 생성. [server/README.md](server/README.md) |
| `UE5_Client/` | UE 5.8 클라이언트. 메타휴먼 페이스 리그 구동 |
| `docs/` | 설계 문서 |

## 클론 후 준비

### MetaHuman 에셋은 저장소에 없다

`Content/MetaHumans/`와 `Content/TestMetaHumanCharacter/`는 `.gitignore`에 있다.
합쳐서 900 MB가 넘어 GitHub LFS 무료 할당(1 GB)을 한 번에 소진한다.

MetaHuman Creator에서 캐릭터를 만들어 프로젝트에 내려받으면 된다.
현재 에셋 이름은 `NewMetaHumanCharacter`이고, `Content/MetaHumans/` 아래에
들어간다. 이름이 다르면 `Content/Maps/Main.umap`의 참조를 다시 잡아야 한다.

### Git LFS

`.uasset` / `.umap` / `.wav` 등 바이너리는 LFS로 관리한다. 클론 전에 설치해라.

```bash
git lfs install
```
