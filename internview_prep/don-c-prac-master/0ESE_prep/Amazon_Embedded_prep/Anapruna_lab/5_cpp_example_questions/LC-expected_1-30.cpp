#include <iostream>
#include <vector>
#include <string>
#include <cassert>
#include <queue>
#include <stack>
#include <unordered_map>
#include <unordered_set>
#include <algorithm>
using namespace std;

// 1. Two Sum
// Input: nums = [2,7,11,15], target = 9
// Output: {0,1}
vector<int> twoSum(vector<int>& nums, int target);

// 2. Reverse Linked List
// Input: 1->2->3->4->5
// Output: 5->4->3->2->1
struct ListNode {
    int val;
    ListNode* next;
    ListNode(int v) : val(v), next(nullptr) {}
};
ListNode* reverseList(ListNode* head);

// 3. Merge Two Sorted Lists
// Input: 1->2->4, 1->3->4
// Output: 1->1->2->3->4->4
ListNode* mergeTwoLists(ListNode* l1, ListNode* l2);

// 4. Remove Duplicates from Sorted Array
// Input: [1,1,2]
// Output: 2, nums = [1,2,_]
int removeDuplicates(vector<int>& nums);

// 5. Move Zeroes
// Input: [0,1,0,3,12]
// Output: [1,3,12,0,0]
void moveZeroes(vector<int>& nums);

// 6. Best Time to Buy and Sell Stock
// Input: [7,1,5,3,6,4]
// Output: 5
int maxProfit(vector<int>& prices);

// 7. Maximum Subarray (Kadane's Algorithm)
// Input: [-2,1,-3,4,-1,2,1,-5,4]
// Output: 6
int maxSubArray(vector<int>& nums);

// 8. Valid Parentheses
// Input: "()[]{}"
// Output: true
bool isValid(string s);

// 9. Implement Queue using Stacks
// Input: push(1), push(2), pop(), peek()
// Output: 1, 2
class MyQueue {
public:
    MyQueue();
    void push(int x);
    int pop();
    int peek();
    bool empty();
};

// 10. Min Stack
// Input: push(-2), push(0), push(-3), getMin()
// Output: -3
class MinStack {
public:
    MinStack();
    void push(int val);
    void pop();
    int top();
    int getMin();
};

// 11. Intersection of Two Arrays
// Input: [1,2,2,1], [2,2]
// Output: [2]
vector<int> intersection(vector<int>& nums1, vector<int>& nums2);

// 12. Linked List Cycle
// Input: 3->2->0->-4 (tail connects to node index 1)
// Output: true
bool hasCycle(ListNode* head);

// 13. Remove Nth Node From End of List
// Input: 1->2->3->4->5, n=2
// Output: 1->2->3->5
ListNode* removeNthFromEnd(ListNode* head, int n);

// 14. Palindrome Linked List
// Input: 1->2->2->1
// Output: true
bool isPalindrome(ListNode* head);

// 15. Binary Tree Inorder Traversal
// Input: [1,null,2,3]
// Output: [1,3,2]
struct TreeNode {
    int val;
    TreeNode* left;
    TreeNode* right;
    TreeNode(int v) : val(v), left(nullptr), right(nullptr) {}
};
vector<int> inorderTraversal(TreeNode* root);

// 16. Maximum Depth of Binary Tree
// Input: [3,9,20,null,null,15,7]
// Output: 3
int maxDepth(TreeNode* root);

// 17. Symmetric Tree
// Input: [1,2,2,3,4,4,3]
// Output: true
bool isSymmetric(TreeNode* root);

// 18. Binary Tree Level Order Traversal
// Input: [3,9,20,null,null,15,7]
// Output: [[3],[9,20],[15,7]]
vector<vector<int>> levelOrder(TreeNode* root);

// 19. Convert Sorted Array to BST
// Input: [-10,-3,0,5,9]
// Output: height balanced BST
TreeNode* sortedArrayToBST(vector<int>& nums);

// 20. Lowest Common Ancestor of BST
// Input: root = [6,2,8,0,4,7,9], p = 2, q = 8
// Output: 6
TreeNode* lowestCommonAncestor(TreeNode* root, TreeNode* p, TreeNode* q);

// 21. Number of 1 Bits
// Input: 00000000000000000000000000001011
// Output: 3
int hammingWeight(uint32_t n);

// 22. Reverse Bits
// Input: 00000010100101000001111010011100
// Output: 964176192
uint32_t reverseBits(uint32_t n);

// 23. Missing Number
// Input: [3,0,1]
// Output: 2
int missingNumber(vector<int>& nums);

// 24. Find All Numbers Disappeared in an Array
// Input: [4,3,2,7,8,2,3,1]
// Output: [5,6]
vector<int> findDisappearedNumbers(vector<int>& nums);

// 25. Single Number
// Input: [4,1,2,1,2]
// Output: 4
int singleNumber(vector<int>& nums);

// 26. Majority Element
// Input: [3,2,3]
// Output: 3
int majorityElement(vector<int>& nums);

// 27. Rotate Array
// Input: [1,2,3,4,5,6,7], k=3
// Output: [5,6,7,1,2,3,4]
void rotate(vector<int>& nums, int k);

// 28. Contains Duplicate
// Input: [1,2,3,1]
// Output: true
bool containsDuplicate(vector<int>& nums);

// 29. Implement strStr()
// Input: haystack = "hello", needle = "ll"
// Output: 2
int strStr(string haystack, string needle);

// 30. First Unique Character in a String
// Input: "leetcode"
// Output: 0
int firstUniqChar(string s);

// =================== 테스트 코드 샘플 ===================
int main() {
    // 1. Two Sum
    vector<int> nums1 = {2,7,11,15};
    auto res1 = twoSum(nums1, 9);
    assert(res1 == vector<int>({0,1}));

    // 2. Reverse Linked List
    ListNode* l1 = new ListNode(1); l1->next = new ListNode(2); l1->next->next = new ListNode(3);
    ListNode* rev = reverseList(l1);
    assert(rev->val == 3 && rev->next->val == 2 && rev->next->next->val == 1);

    // 3. Merge Two Sorted Lists
    ListNode* m1 = new ListNode(1); m1->next = new ListNode(2); m1->next->next = new ListNode(4);
    ListNode* m2 = new ListNode(1); m2->next = new ListNode(3); m2->next->next = new ListNode(4);
    ListNode* merged = mergeTwoLists(m1, m2);
    assert(merged->val == 1 && merged->next->val == 1 && merged->next->next->val == 2);

    // 4. Remove Duplicates from Sorted Array
    vector<int> nums2 = {1,1,2};
    int len = removeDuplicates(nums2);
    assert(len == 2 && nums2[0] == 1 && nums2[1] == 2);

    // 5. Move Zeroes
    vector<int> nums3 = {0,1,0,3,12};
    moveZeroes(nums3);
    assert(nums3 == vector<int>({1,3,12,0,0}));

    // 6. Best Time to Buy and Sell Stock
    vector<int> prices = {7,1,5,3,6,4};
    assert(maxProfit(prices) == 5);

    // 7. Maximum Subarray
    vector<int> arr = {-2,1,-3,4,-1,2,1,-5,4};
    assert(maxSubArray(arr) == 6);

    // 8. Valid Parentheses
    assert(isValid("()[]{}"));
    assert(!isValid("(]"));

    // 9. Implement Queue using Stacks
    MyQueue q;
    q.push(1); q.push(2);
    assert(q.peek() == 1);
    assert(q.pop() == 1);
    assert(!q.empty());

    // 10. Min Stack
    MinStack ms;
    ms.push(-2); ms.push(0); ms.push(-3);
    assert(ms.getMin() == -3);
    ms.pop();
    assert(ms.top() == 0);
    assert(ms.getMin() == -2);

    // 11. Intersection of Two Arrays
    vector<int> nums4 = {1,2,2,1}, nums5 = {2,2};
    auto inter = intersection(nums4, nums5);
    assert(inter == vector<int>({2}));

    // 12. Linked List Cycle
    ListNode* cyc1 = new ListNode(3); ListNode* cyc2 = new ListNode(2); ListNode* cyc3 = new ListNode(0); ListNode* cyc4 = new ListNode(-4);
    cyc1->next = cyc2; cyc2->next = cyc3; cyc3->next = cyc4; cyc4->next = cyc2;
    assert(hasCycle(cyc1));

    // 13. Remove Nth Node From End of List
    ListNode* r1 = new ListNode(1); r1->next = new ListNode(2); r1->next->next = new ListNode(3); r1->next->next->next = new ListNode(4); r1->next->next->next->next = new ListNode(5);
    ListNode* rres = removeNthFromEnd(r1, 2);
    assert(rres->val == 1 && rres->next->next->val == 3 && rres->next->next->next->val == 5);

    // 14. Palindrome Linked List
    ListNode* p1 = new ListNode(1); p1->next = new ListNode(2); p1->next->next = new ListNode(2); p1->next->next->next = new ListNode(1);
    assert(isPalindrome(p1));

    // 15. Binary Tree Inorder Traversal
    TreeNode* t1 = new TreeNode(1); t1->right = new TreeNode(2); t1->right->left = new TreeNode(3);
    auto inorder = inorderTraversal(t1);
    assert(inorder == vector<int>({1,3,2}));

    // 16. Maximum Depth of Binary Tree
    TreeNode* t2 = new TreeNode(3); t2->left = new TreeNode(9); t2->right = new TreeNode(20); t2->right->left = new TreeNode(15); t2->right->right = new TreeNode(7);
    assert(maxDepth(t2) == 3);

    // 17. Symmetric Tree
    TreeNode* t3 = new TreeNode(1); t3->left = new TreeNode(2); t3->right = new TreeNode(2); t3->left->left = new TreeNode(3); t3->left->right = new TreeNode(4); t3->right->left = new TreeNode(4); t3->right->right = new TreeNode(3);
    assert(isSymmetric(t3));

    // 18. Binary Tree Level Order Traversal
    TreeNode* t4 = new TreeNode(3); t4->left = new TreeNode(9); t4->right = new TreeNode(20); t4->right->left = new TreeNode(15); t4->right->right = new TreeNode(7);
    auto level = levelOrder(t4);
    assert(level.size() == 3 && level[0][0] == 3 && level[2][0] == 15);

    // 19. Convert Sorted Array to BST
    vector<int> nums6 = {-10,-3,0,5,9};
    TreeNode* bst = sortedArrayToBST(nums6);
    assert(bst && bst->val == 0);

    // 20. Lowest Common Ancestor of BST
    TreeNode* root = new TreeNode(6);
    root->left = new TreeNode(2); root->right = new TreeNode(8);
    root->left->left = new TreeNode(0); root->left->right = new TreeNode(4);
    root->right->left = new TreeNode(7); root->right->right = new TreeNode(9);
    TreeNode* lca = lowestCommonAncestor(root, root->left, root->right);
    assert(lca->val == 6);

    // 21. Number of 1 Bits
    assert(hammingWeight(0b1011) == 3);

    // 22. Reverse Bits
    assert(reverseBits(0b00000010100101000001111010011100) == 964176192);

    // 23. Missing Number
    vector<int> nums7 = {3,0,1};
    assert(missingNumber(nums7) == 2);

    // 24. Find All Numbers Disappeared in an Array
    vector<int> nums8 = {4,3,2,7,8,2,3,1};
    auto disappeared = findDisappearedNumbers(nums8);
    assert(disappeared == vector<int>({5,6}));

    // 25. Single Number
    vector<int> nums9 = {4,1,2,1,2};
    assert(singleNumber(nums9) == 4);

    // 26. Majority Element
    vector<int> nums10 = {3,2,3};
    assert(majorityElement(nums10) == 3);

    // 27. Rotate Array
    vector<int> nums11 = {1,2,3,4,5,6,7};
    rotate(nums11, 3);
    assert(nums11 == vector<int>({5,6,7,1,2,3,4}));

    // 28. Contains Duplicate
    vector<int> nums12 = {1,2,3,1};
    assert(containsDuplicate(nums12));

    // 29. Implement strStr()
    assert(strStr("hello", "ll") == 2);

    // 30. First Unique Character in a String
    assert(firstUniqChar("leetcode") == 0);

    cout << "All tests PASS" << endl;
    return 0;
}