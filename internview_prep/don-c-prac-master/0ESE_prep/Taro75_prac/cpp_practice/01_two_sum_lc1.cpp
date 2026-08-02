#include <vector>
#include <iostream>
#include <algorithm>
using namespace std;

// LeetCode 1. Two Sum
// Input: nums = [2,7,11,15], target = 9
// Output: [0,1]
class Solution {
public:
    vector<int> twoSum(vector<int>& nums, int target) {
        // TODO: Implement
        unordered_map<int, int> m;
        for (int i = 0; i < nums.size(); ++i)
        {
            int complement = target - nums[i];
            if (m.count(complement)) {
                return {m[complement], i};
            }
            m[nums[i]] = i;
        }
        return {};
    }
};

// Helper to check if two vectors are equal regardless of order
bool same_indices(const vector<int>& a, const vector<int>& b) {
    return (a.size() == b.size()) && ((a[0] == b[0] && a[1] == b[1]) || (a[0] == b[1] && a[1] == b[0]));
}

int main() {
    Solution sol;
    int pass_cnt = 0;

    // TC1
    vector<int> nums1 = {2,7,11,15};
    auto res1 = sol.twoSum(nums1, 9);
    if (same_indices(res1, {0,1})) { cout << "TC1 PASS" << endl; pass_cnt++; }
    else { cout << "TC1 FAIL" << endl; }

    // TC2
    vector<int> nums2 = {3,2,4};
    auto res2 = sol.twoSum(nums2, 6);
    if (same_indices(res2, {1,2})) { cout << "TC2 PASS" << endl; pass_cnt++; }
    else { cout << "TC2 FAIL" << endl; }

    // TC3
    vector<int> nums3 = {3,3};
    auto res3 = sol.twoSum(nums3, 6);
    if (same_indices(res3, {0,1})) { cout << "TC3 PASS" << endl; pass_cnt++; }
    else { cout << "TC3 FAIL" << endl; }

    // TC4
    vector<int> nums4 = {1,5,3,7};
    auto res4 = sol.twoSum(nums4, 8);
    if (same_indices(res4, {0,3})) { cout << "TC4 PASS" << endl; pass_cnt++; }
    else { cout << "TC4 FAIL" << endl; }

    // TC5
    vector<int> nums5 = {0,4,3,0};
    auto res5 = sol.twoSum(nums5, 0);
    if (same_indices(res5, {0,3})) { cout << "TC5 PASS" << endl; pass_cnt++; }
    else { cout << "TC5 FAIL" << endl; }

    // TC6
    vector<int> nums6 = {-1,-2,-3,-4,-5};
    auto res6 = sol.twoSum(nums6, -8);
    if (same_indices(res6, {2,4})) { cout << "TC6 PASS" << endl; pass_cnt++; }
    else { cout << "TC6 FAIL" << endl; }

    // TC7
    vector<int> nums7 = {1,2,5,6};
    auto res7 = sol.twoSum(nums7, 7);
    if (same_indices(res7, {1,3})) { cout << "TC7 PASS" << endl; pass_cnt++; }
    else { cout << "TC7 FAIL" << endl; }

    // TC8
    vector<int> nums8 = {1,3,4,2};
    auto res8 = sol.twoSum(nums8, 6);
    if (same_indices(res8, {2,3})) { cout << "TC8 PASS" << endl; pass_cnt++; }
    else { cout << "TC8 FAIL" << endl; }

    // TC9
    vector<int> nums9 = {5,75,25};
    auto res9 = sol.twoSum(nums9, 100);
    if (same_indices(res9, {1,2})) { cout << "TC9 PASS" << endl; pass_cnt++; }
    else { cout << "TC9 FAIL" << endl; }

    // TC10
    vector<int> nums10 = {0,1,2,3,4,5,6,7,8,9,10};
    auto res10 = sol.twoSum(nums10, 19);
    if (same_indices(res10, {9,10})) { cout << "TC10 PASS" << endl; pass_cnt++; }
    else { cout << "TC10 FAIL" << endl; }

    cout << "Total Passed: " << pass_cnt << "/10" << endl;
    return 0;
}