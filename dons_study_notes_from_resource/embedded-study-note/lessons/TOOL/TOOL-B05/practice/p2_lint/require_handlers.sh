# require_handlers.sh — 링크된 이미지에서 "아직 weak 기본값에 묶인" 핸들러를 찾는 CI 가드 (연습 2)
# 사용: source require_handlers.sh; require_handlers <exe|elf> NAME...
#   NAME 마다 최종 이미지에서 강한(strong) 정의가 이겼는지 본다.
#   weak 로 남았거나 아예 없으면 "UNBOUND NAME" 을 찍고, 하나라도 있으면 return 1.
#   Mach-O: nm -m 의 "weak external _NAME"  /  ELF: nm 의 "W NAME"
require_handlers() {
    local img=$1 name kind rc=0 tab
    shift
    case "$(file -b "$img")" in
    Mach-O*) kind=macho; tab=$(nm -m "$img") ;;
    ELF*)    kind=elf;   tab=$(nm "$img") ;;
    *) echo "UNKNOWN FORMAT $img"; return 2 ;;
    esac
    for name in "$@"; do
        if [ "$kind" = macho ]; then
            printf '%s\n' "$tab" | grep -Eq "\) external _${name}\$" && continue
        else
            printf '%s\n' "$tab" | grep -Eq " T ${name}\$" && continue
        fi
        echo "UNBOUND $name"
        rc=1
    done
    return $rc
}
