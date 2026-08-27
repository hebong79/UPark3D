# ContentDelivery — git 으로 못 오는 에셋을 배달하는 임시 통로

`.gitignore:35` 가 `Park3D/Content/` 를 통째로 제외한다(주석: *"Content는 SVN이 authoritative"*).
그래서 **코드만 git 으로 받으면 에셋 변경이 안 따라온다.** 이 폴더는 그 구멍을 메우는
**단방향 배달 스냅샷**이다.

```
python ContentDelivery/apply.py            # 무엇이 바뀌는지 보여만 준다
python ContentDelivery/apply.py --write    # 덮어쓴다(기존 파일은 _backup/<시각>/ 으로 옮긴다)
```

구조가 `Park3D/Content` 와 같으므로 스크립트를 못 쓰면 `Content` 폴더를 통째로
`Park3D/Content` 위에 덮어써도 결과는 같다.

---

## ⚠ 이 폴더의 위험 — 진실원이 둘이 된다

SVN 이 정본인데 여기 사본이 생기면, **누군가 SVN 에서 에셋을 고친 뒤 이 폴더의 낡은
사본으로 덮어써 그 수정을 날릴 수 있다.** 그래서 세 가지를 지킨다.

1. **여기 있는 것은 정본이 아니다.** SVN(또는 각 PC 의 `Park3D/Content`)이 정본이고,
   이건 "git 으로만 받는 PC" 를 위한 배달본이다.
2. **낡았는지 해시로 판정한다.** `MANIFEST.txt` 에 sha256 이 있다.
   ```
   python ContentDelivery/make_manifest.py    # 마지막 칸이 전부 '일치' 여야 한다
   ```
   `**다름**` 이 뜨면 이 배달본이 낡은 것이다 — SVN 쪽을 확인하고 다시 담아라.
3. **에셋 변경이 끝나면 이 폴더를 비우는 것이 원칙이다.** 임시 통로이지 보관소가 아니다.
   SVN 에 반영된 뒤에는 지워도 된다.

`apply.py` 는 덮어쓰기 전에 기존 파일을 `_backup/<시각>/` 으로 복사하므로 되돌릴 수 있다.

---

## 지금 담긴 것 (2026-08-27)

| 파일 | 왜 |
|------|-----|
| `Content/Actors/Car/Plates/Materials/M_PlateFront.uasset` | 번호판 양각(SDF 릴리프) 머티리얼. Custom(HLSL) 노드 + Substrate Slab + 파라미터 12종이 **이 에셋 안에** 있다 |

이 파일 없이 코드만 받으면 깨지지는 않는다 — `NumberSDF` 파라미터를 못 찾아
**예전 평면 위젯 표시로 폴백**한다. 로그로 갈린다.

```
[CarPlate] SDF ... front=1 back=1 폴백위젯=0/0   ← 정상
[CarPlate] SDF ... front=0 back=0 폴백위젯=1/1   ← 머티리얼이 안 왔다
```

### 스크립트로 다시 만드는 길도 있다

에셋을 받는 대신 **에디터에서 재생성**해도 된다. 멱등하게 짜여 여러 번 돌려도 결과가 같다.

```
MSYS_NO_PATHCONV=1 python Tools/plate_sdf/build_material.py
```

다만 이 스크립트는 **그 PC 에 있는 `M_PlateFront` 위에** 노드를 얹는다. 그 PC 의 원본이
여기와 다르면 결과도 달라진다 — 검증된 것과 똑같은 결과를 원하면 `apply.py` 로 에셋을 받아라.

---

## 이것만으로 끝나지 않는다

에셋을 받아도 **C++ 이 바뀌었고 `Text3D` 플러그인이 빠져서 exe 교체로는 안 된다.**
다른 PC 에서 할 일 전체는 이렇다.

1. `git pull` — 소스 · 아틀라스(`Save/Config/plate_glyph_sdf.png`) · 메트릭 · 툴
2. `python ContentDelivery/apply.py --write` — 머티리얼
3. `BuildPackage.bat` — C++ 빌드 + 전체 재쿡

자세한 내용은 [Docs/20260827_180238_번호판_양각_SDF_머티리얼_전환_구현.md](../Docs/20260827_180238_번호판_양각_SDF_머티리얼_전환_구현.md).
