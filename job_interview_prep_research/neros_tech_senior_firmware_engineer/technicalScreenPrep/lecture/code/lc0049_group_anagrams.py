# --- solution ---
from collections import defaultdict


def groupAnagrams(strs: list[str]) -> list[list[str]]:
    groups = defaultdict(list)
    for s in strs:
        groups["".join(sorted(s))].append(s)   # 정렬한 문자열이 같으면 같은 그룹
    return list(groups.values())
# --- end ---


def test_lc49():
    got = sorted(sorted(g) for g in groupAnagrams(["eat", "tea", "tan", "ate", "nat", "bat"]))
    assert got == [["ate", "eat", "tea"], ["bat"], ["nat", "tan"]]
