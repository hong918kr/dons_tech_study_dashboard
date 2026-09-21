#include <iostream>
#include <vector>
using namespace std;

// LeetCode 2. Add Two Numbers
struct ListNode {
    int val;
    ListNode* next;
    ListNode(int v) : val(v), next(nullptr) {}
};

class Solution {
public:
    ListNode* addTwoNumbers(ListNode* l1, ListNode* l2) {
        // TODO: Implement
        return nullptr;
    }
};

// Helper to create linked list from vector
ListNode* make_list(const vector<int>& vals) {
    ListNode dummy(0);
    ListNode* cur = &dummy;
    for (int v : vals) {
        cur->next = new ListNode(v);
        cur = cur->next;
    }
    return dummy.next;
}

// Helper to compare two linked lists
bool same_list(ListNode* a, ListNode* b) {
    while (a && b) {
        if (a->val != b->val) return false;
        a = a->next; b = b->next;
    }
    return !a && !b;
}

// Helper to print list (for debugging)
void print_list(ListNode* node) {
    while (node) {
        cout << node->val;
        if (node->next) cout << "->";
        node = node->next;
    }
    cout << endl;
}

int main() {
    Solution sol;
    int pass_cnt = 0;

    // TC1
    ListNode* l1 = make_list({2,4,3});
    ListNode* l2 = make_list({5,6,4});
    ListNode* res = sol.addTwoNumbers(l1, l2);
    if (same_list(res, make_list({7,0,8}))) {
        cout << "TC1 PASS" << endl; pass_cnt++;
    } else {
        cout << "TC1 FAIL" << endl;
    }

    // TC2
    l1 = make_list({0});
    l2 = make_list({0});
    res = sol.addTwoNumbers(l1, l2);
    if (same_list(res, make_list({0}))) {
        cout << "TC2 PASS" << endl; pass_cnt++;
    } else {
        cout << "TC2 FAIL" << endl;
    }

    // TC3
    l1 = make_list({9,9,9,9,9,9,9});
    l2 = make_list({9,9,9,9});
    res = sol.addTwoNumbers(l1, l2);
    if (same_list(res, make_list({8,9,9,9,0,0,0,1}))) {
        cout << "TC3 PASS" << endl; pass_cnt++;
    } else {
        cout << "TC3 FAIL" << endl;
    }

    // TC4
    l1 = make_list({1,8});
    l2 = make_list({0});
    res = sol.addTwoNumbers(l1, l2);
    if (same_list(res, make_list({1,8}))) {
        cout << "TC4 PASS" << endl; pass_cnt++;
    } else {
        cout << "TC4 FAIL" << endl;
    }

    // TC5
    l1 = make_list({5});
    l2 = make_list({5});
    res = sol.addTwoNumbers(l1, l2);
    if (same_list(res, make_list({0,1}))) {
        cout << "TC5 PASS" << endl; pass_cnt++;
    } else {
        cout << "TC5 FAIL" << endl;
    }

    // TC6
    l1 = make_list({1,0,1});
    l2 = make_list({9,9});
    res = sol.addTwoNumbers(l1, l2);
    if (same_list(res, make_list({0,0,2}))) {
        cout << "TC6 PASS" << endl; pass_cnt++;
    } else {
        cout << "TC6 FAIL" << endl;
    }

    // TC7
    l1 = make_list({2,4,9});
    l2 = make_list({5,6,4,9});
    res = sol.addTwoNumbers(l1, l2);
    if (same_list(res, make_list({7,0,4,0,1}))) {
        cout << "TC7 PASS" << endl; pass_cnt++;
    } else {
        cout << "TC7 FAIL" << endl;
    }

    // TC8
    l1 = make_list({9,9});
    l2 = make_list({1});
    res = sol.addTwoNumbers(l1, l2);
    if (same_list(res, make_list({0,0,1}))) {
        cout << "TC8 PASS" << endl; pass_cnt++;
    } else {
        cout << "TC8 FAIL" << endl;
    }

    // TC9
    l1 = make_list({1});
    l2 = make_list({9,9,9});
    res = sol.addTwoNumbers(l1, l2);
    if (same_list(res, make_list({0,0,0,1}))) {
        cout << "TC9 PASS" << endl; pass_cnt++;
    } else {
        cout << "TC9 FAIL" << endl;
    }

    // TC10
    l1 = make_list({0,1});
    l2 = make_list({0,1,2});
    res = sol.addTwoNumbers(l1, l2);
    if (same_list(res, make_list({0,2,2}))) {
        cout << "TC10 PASS" << endl; pass_cnt++;
    } else {
        cout << "TC10 FAIL" << endl;
    }

    cout << "Total Passed: " << pass_cnt << "/10" << endl;
    return 0;
}