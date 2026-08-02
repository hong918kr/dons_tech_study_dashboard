#include <iostream>
#include <cassert>
#include <cstring>
#include <vector>
#include <climits>
using namespace std;

// 1. 배열 뒤집기
void reverse_array(vector<int>& arr) {
    int n = arr.size();
    for (int i = 0; i < n/2; ++i)
        swap(arr[i], arr[n-1-i]);
}

// 2. 문자열 뒤집기
void reverse_string(char* str) {
    int n = strlen(str);
    for (int i = 0; i < n/2; ++i)
        swap(str[i], str[n-1-i]);
}

// 3. 배열 중복 제거 (in-place, 정렬된 배열 가정)
void remove_duplicates(vector<int>& arr) {
    if (arr.empty()) return;
    int idx = 1;
    for (size_t i = 1; i < arr.size(); ++i) {
        if (arr[i] != arr[idx-1])
            arr[idx++] = arr[i];
    }
    arr.resize(idx);
}

// 4. 배열 오른쪽으로 k만큼 회전
void rotate_array(vector<int>& arr, int k) {
    int n = arr.size();
    if (n == 0) return;
    k = k % n;
    reverse(arr.begin(), arr.end());
    reverse(arr.begin(), arr.begin() + k);
    reverse(arr.begin() + k, arr.end());
}

// 5. 슬라이딩 윈도우 최대값의 합 (윈도우 크기 k)
int sliding_window_max(const vector<int>& arr, int k) {
    if (arr.empty() || k <= 0) return 0;
    int n = arr.size();
    int sum = 0;
    for (int i = 0; i <= n - k; ++i) {
        int mx = arr[i];
        for (int j = 1; j < k; ++j)
            if (arr[i+j] > mx) mx = arr[i+j];
        sum += mx;
    }
    return sum;
}

// 6. 문자열을 int로 변환 (atoi)
int my_atoi(const char* str) {
    int res = 0, sign = 1, i = 0;
    if (str[0] == '-') { sign = -1; i++; }
    while (str[i]) {
        if (str[i] >= '0' && str[i] <= '9')
            res = res * 10 + (str[i] - '0');
        else
            break;
        i++;
    }
    return sign * res;
}

// 7. int를 문자열로 변환 (itoa)
void my_itoa(int val, char* buf) {
    bool neg = false;
    if (val < 0) { neg = true; val = -val; }
    int i = 0;
    do {
        buf[i++] = '0' + (val % 10);
        val /= 10;
    } while (val);
    if (neg) buf[i++] = '-';
    buf[i] = '\0';
    // reverse the string
    for (int j = 0; j < i/2; ++j)
        swap(buf[j], buf[i-1-j]);
}

// 8. 부분 문자열 찾기 (strstr)
const char* my_strstr(const char* haystack, const char* needle) {
    if (!*needle) return haystack;
    int n = strlen(haystack), m = strlen(needle);
    for (int i = 0; i <= n - m; ++i) {
        int j = 0;
        while (j < m && haystack[i+j] == needle[j]) j++;
        if (j == m) return haystack + i;
    }
    return nullptr;
}

// 9. 문자열 비교 (strcmp)
int my_strcmp(const char* s1, const char* s2) {
    while (*s1 && *s1 == *s2) { s1++; s2++; }
    return (unsigned char)*s1 - (unsigned char)*s2;
}

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
    assert(max_sum == 29); // 3+3+5+5+6+7=29

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