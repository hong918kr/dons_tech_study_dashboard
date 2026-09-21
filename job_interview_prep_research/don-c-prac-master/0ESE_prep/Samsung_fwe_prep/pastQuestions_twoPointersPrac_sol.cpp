#include <bits/stdc++.h>
using namespace std;

// Helpers
static bool eqVec(const vector<int>& a, const vector<int>& b) { return a == b; }
static void printVec(const vector<int>& v) {
    cout << "[";
    for (size_t i = 0; i < v.size(); ++i) {
        if (i) cout << ",";
        cout << v[i];
    }
    cout << "]";
}
static bool expectVec(const char* name, const vector<int>& got, const vector<int>& want) {
    bool ok = eqVec(got, want);
    cout << name << ": " << (ok ? "PASS" : "FAIL") << " | got=";
    printVec(got);
    cout << " expect=";
    printVec(want);
    cout << "\n";
    return ok;
}
static bool expectLenAndPrefix(const char* name, const vector<int>& a, int len, const vector<int>& prefix) {
    bool ok = (len == (int)prefix.size());
    if (ok) {
        for (int i = 0; i < len; ++i) if (a[i] != prefix[i]) { ok = false; break; }
    }
    cout << name << ": " << (ok ? "PASS" : "FAIL") << " | len=" << len << " expect_len=" << prefix.size()
         << " prefix_got=";
    vector<int> pre(a.begin(), a.begin() + len);
    printVec(pre);
    cout << " expect_prefix=";
    printVec(prefix);
    cout << "\n";
    return ok;
}

/*
Problem 1: Move zeros to front (stable)
- Move all 0s to the beginning, preserving the relative order of non-zero elements.
- Example:
  Input:  [1,0,2,3,0,0]
  Output: [0,0,0,1,2,3]
*/
void moveZerosToFrontStable(vector<int>& a) {
    int n = (int)a.size();
    int zeros = 0;
    for (int x : a) if (x == 0) ++zeros;
    int w = 0;
    // write zeros
    for (; w < zeros; ++w) a[w] = 0;
    // write non-zeros in original order
    for (int x : a) if (x != 0) a[w++] = x;
}

/*
Problem 2: Move zeros to end (stable)
- Move all 0s to the end, preserving the relative order of non-zero elements.
- Example:
  Input:  [0,1,0,3,12]
  Output: [1,3,12,0,0]
*/
void moveZerosToEndStable(vector<int>& a) {
    int w = 0;
    for (int x : a) if (x != 0) a[w++] = x;
    while (w < (int)a.size()) a[w++] = 0;
}

/*
Problem 3: Move target value v to end (stable)
- Move all occurrences of v to the end, preserving the order of non-v elements.
- Example:
  Input:  [2,5,3,5,7], v=5
  Output: [2,3,7,5,5]
*/
void moveTargetToEndStable(vector<int>& a, int v) {
    int w = 0;
    for (int x : a) if (x != v) a[w++] = x;
    while (w < (int)a.size()) a[w++] = v;
}

/*
Problem 4: Remove element v (return new length)
- Remove all occurrences of v in-place; return the new length. Tail data can be ignored.
- Example:
  Input:  [3,2,2,3], v=3
  Output: len=2, a[0..1]=[2,2]
*/
int removeElement(vector<int>& a, int v) {
    int w = 0;
    for (int x : a) if (x != v) a[w++] = x;
    return w;
}

/*
Problem 5: Remove duplicates from sorted array
- Keep one occurrence for each value. Return the new length.
- Example:
  Input:  [0,0,1,1,1,2,2,3]
  Output: len=4, a[0..3]=[0,1,2,3]
*/
int removeDuplicatesSorted(vector<int>& a) {
    if (a.empty()) return 0;
    int w = 1;
    for (int i = 1; i < (int)a.size(); ++i) {
        if (a[i] != a[w - 1]) a[w++] = a[i];
    }
    return w;
}

/*
Problem 6: Partition by parity (not-stable)
- Place even numbers on the left and odd numbers on the right. Order not preserved.
- Example:
  Input:  [3,1,2,4]
  Output: [2,4,1,3] (or any valid partition)
*/
void partitionByParity(vector<int>& a) {
    int i = 0, j = (int)a.size() - 1;
    auto isEven = [](int x) { return (x & 1) == 0; };
    while (i < j) {
        while (i < j && isEven(a[i])) ++i;
        while (i < j && !isEven(a[j])) --j;
        if (i < j) swap(a[i++], a[j--]);
    }
}

/*
Problem 7: Dutch National Flag (values only 0,1,2)
- Sort array so that all 0s, then 1s, then 2s. One pass, O(1) extra.
- Example:
  Input:  [2,0,2,1,1,0]
  Output: [0,0,1,1,2,2]
*/
void dutchFlag012(vector<int>& a) {
    int low = 0, mid = 0, high = (int)a.size() - 1;
    while (mid <= high) {
        if (a[mid] == 0) swap(a[low++], a[mid++]);
        else if (a[mid] == 1) ++mid;
        else swap(a[mid], a[high--]);
    }
}

/*
Problem 8: Partition around pivot p (not-stable)
- Reorder so that all elements <= p are on the left, and > p on the right.
- Example:
  Input:  [3,5,2,1,6,4], p=3
  Output: [3,2,1,|,5,6,4]  (bar is conceptual)
*/
void partitionAroundPivot(vector<int>& a, int p) {
    int i = 0, j = (int)a.size() - 1;
    while (i <= j) {
        if (a[i] <= p) { ++i; continue; }
        if (a[j] > p) { --j; continue; }
        swap(a[i++], a[j--]);
    }
}

/*
Problem 9: Move negatives to front (not-stable)
- Put all negatives first, non-negatives after. Order not preserved.
- Example:
  Input:  [-1,3,-2,4,0]
  Output: [-1,-2,0,4,3] (any partition with negatives first is OK)
*/
void moveNegativesToFront(vector<int>& a) {
    int i = 0, j = (int)a.size() - 1;
    while (i < j) {
        while (i < j && a[i] < 0) ++i;
        while (i < j && a[j] >= 0) --j;
        if (i < j) swap(a[i++], a[j--]);
    }
}

/*
Problem 10: Remove duplicates from sorted array, allow at most k copies
- Keep at most k duplicates for each value. Return the new length.
- Example (k=2):
  Input:  [1,1,1,2,2,3]
  Output: len=5, a[0..4]=[1,1,2,2,3]
*/
int removeDuplicatesAllowK(vector<int>& a, int k) {
    if (a.empty()) return 0;
    int w = 0;
    for (int x : a) {
        if (w < k || a[w - k] != x) a[w++] = x;
    }
    return w;
}

// ---------------- Tests ----------------
int main() {
    int total = 0, passed = 0;

    // Problem 1
    {
        ++total;
        vector<int> a{1,0,2,3,0,0};
        vector<int> want{0,0,0,1,2,3};
        moveZerosToFrontStable(a);
        passed += expectVec("P1 moveZerosToFrontStable", a, want);
    }

    // Problem 2
    {
        ++total;
        vector<int> a{0,1,0,3,12};
        vector<int> want{1,3,12,0,0};
        moveZerosToEndStable(a);
        passed += expectVec("P2 moveZerosToEndStable", a, want);
    }

    // Problem 3
    {
        ++total;
        vector<int> a{2,5,3,5,7};
        vector<int> want{2,3,7,5,5};
        moveTargetToEndStable(a, 5);
        passed += expectVec("P3 moveTargetToEndStable", a, want);
    }

    // Problem 4
    {
        ++total;
        vector<int> a{3,2,2,3};
        int len = removeElement(a, 3);
        passed += expectLenAndPrefix("P4 removeElement", a, len, {2,2});
    }

    // Problem 5
    {
        ++total;
        vector<int> a{0,0,1,1,1,2,2,3};
        int len = removeDuplicatesSorted(a);
        passed += expectLenAndPrefix("P5 removeDuplicatesSorted", a, len, {0,1,2,3});
    }

    // Problem 6
    {
        ++total;
        vector<int> a{3,1,2,4,5,6};
        partitionByParity(a);
        // verify all evens first then odds
        bool ok = true;
        bool seenOdd = false;
        for (int x : a) {
            if ((x & 1) == 1) seenOdd = true;
            if (seenOdd && (x % 2 == 0)) { ok = false; break; }
        }
        cout << "P6 partitionByParity: " << (ok ? "PASS" : "FAIL") << " | got=";
        printVec(a); cout << "\n";
        passed += ok;
    }

    // Problem 7
    {
        ++total;
        vector<int> a{2,0,2,1,1,0};
        vector<int> want{0,0,1,1,2,2};
        dutchFlag012(a);
        passed += expectVec("P7 dutchFlag012", a, want);
    }

    // Problem 8
    {
        ++total;
        vector<int> a{3,5,2,1,6,4};
        int p = 3;
        partitionAroundPivot(a, p);
        bool ok = true;
        bool seenRight = false;
        for (int x : a) {
            if (!seenRight && x > p) seenRight = true;
            if (seenRight && x <= p) { ok = false; break; }
        }
        cout << "P8 partitionAroundPivot: " << (ok ? "PASS" : "FAIL") << " | got=";
        printVec(a); cout << " pivot=" << p << "\n";
        passed += ok;
    }

    // Problem 9
    {
        ++total;
        vector<int> a{-1,3,-2,4,0};
        moveNegativesToFront(a);
        bool ok = true;
        bool seenNonNeg = false;
        for (int x : a) {
            if (x >= 0) seenNonNeg = true;
            if (seenNonNeg && x < 0) { ok = false; break; }
        }
        cout << "P9 moveNegativesToFront: " << (ok ? "PASS" : "FAIL") << " | got=";
        printVec(a); cout << "\n";
        passed += ok;
    }

    // Problem 10
    {
        ++total;
        vector<int> a{1,1,1,2,2,3};
        int len = removeDuplicatesAllowK(a, 2);
        passed += expectLenAndPrefix("P10 removeDuplicatesAllowK(k=2)", a, len, {1,1,2,2,3});
    }

    cout << "Total Passed: " << passed << "/" << total << "\n";
    return 0;
}

/*
Build (Linux):
  g++ -std=c++17 -O2 -Wall -Wextra two_pointers_practice.cpp -o two_pointers_practice
Run:
  ./two_pointers_practice
*/