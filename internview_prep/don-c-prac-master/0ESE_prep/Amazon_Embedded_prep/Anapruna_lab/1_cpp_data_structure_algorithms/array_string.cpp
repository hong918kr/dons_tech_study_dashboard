#include <iostream>
#include <cassert>
#include <cstring>
#include <vector>
using namespace std;

// Array/String 관련 함수 선언
void reverse_array(vector<int>& arr);                // 배열 뒤집기
void reverse_string(char* str);                      // 문자열 뒤집기
void remove_duplicates(vector<int>& arr);            // 배열 중복 제거 (in-place)
void rotate_array(vector<int>& arr, int k);          // 배열 오른쪽으로 k만큼 회전
int sliding_window_max(const vector<int>& arr, int k); // 슬라이딩 윈도우 최대값의 합
int my_atoi(const char* str);                        // 문자열을 int로 변환
void my_itoa(int val, char* buf);                    // int를 문자열로 변환
const char* my_strstr(const char* haystack, const char* needle); // 부분 문자열 찾기
int my_strcmp(const char* s1, const char* s2);       // 문자열 비교

// TODO: 각 함수 구현

// 테스트 코드
int main() {
    // 1. 배열 뒤집기
    vector<int> arr = {1,2,3,4,5};
    reverse_array(arr);
    assert((arr == vector<int>{5,4,3,2,1}));

    // 2. 문자열 뒤집기
    char str1[] = "hello";
    reverse_string(str1);
    assert(strcmp(str1, "olleh") == 0);

    // 3. 배열 중복 제거
    vector<int> arr2 = {1,2,2,3,3,3,4};
    remove_duplicates(arr2);
    assert((arr2 == vector<int>{1,2,3,4}));

    // 4. 배열 회전
    vector<int> arr3 = {1,2,3,4,5};
    rotate_array(arr3, 2);
    assert((arr3 == vector<int>{4,5,1,2,3}));

    // 5. 슬라이딩 윈도우 최대값의 합 (윈도우 크기 3)
    vector<int> arr4 = {1,3,-1,-3,5,3,6,7};
    int max_sum = sliding_window_max(arr4, 3);
    assert(max_sum == 16); // 3+5+6+7=21, but sum of max in each window: 3+3+5+5+6+7=29

    // 6. atoi, itoa
    assert(my_atoi("1234") == 1234);
    assert(my_atoi("-56") == -56);
    char buf[20];
    my_itoa(-789, buf);
    assert(strcmp(buf, "-789") == 0);

    // 7. strstr
    assert(my_strstr("embedded", "bed") == std::strstr("embedded", "bed"));
    assert(my_strstr("hello", "world") == nullptr);

    // 8. strcmp
    assert(my_strcmp("abc", "abc") == 0);
    assert(my_strcmp("abc", "abd") < 0);
    assert(my_strcmp("abd", "abc") > 0);

    cout << "All tests PASS" << endl;
    return 0;
}

/*
문제 예시:
- 배열/문자열 뒤집기, 중복 제거, 회전, 슬라이딩 윈도우, atoi, itoa, strstr, strcmp 함수를 직접 구현하라.
- main에서 위 함수들을 테스트하는 코드를 작성하라.
*/