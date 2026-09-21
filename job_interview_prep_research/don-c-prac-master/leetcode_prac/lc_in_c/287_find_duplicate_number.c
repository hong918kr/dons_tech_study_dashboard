/*
Given an array of integers nums containing n + 1 integers where each integer is in the range [1, n] inclusive.

There is only one repeated number in nums, return this repeated number.

You must solve the problem without modifying the array nums and using only constant extra space.

 

Example 1:

Input: nums = [1,3,4,2,2]
Output: 2
Example 2:

Input: nums = [3,1,3,4,2]
Output: 3
Example 3:

Input: nums = [3,3,3,3,3]
Output: 3
*/


int findDuplicate(int* nums, int numsSize) {
    /*
You're encountering the error "variable-sized object may not be initialized except with an empty initializer" because you're trying to initialize a Variable Length Array (VLA) with an initializer other than {}. VLAs, which have sizes determined at runtime, have a limitation in C: they can only be initialized with an empty initializer list.
    */
    int i =0;
    int arr[numsSize];
    memset(arr, 0, numsSize * sizeof(int));
    
    while (i < numsSize)
    {
        if (arr[*nums] == 0) {
            arr[*nums]++;
        }
        else {
            return *nums;
        }
        ++nums;
        ++i;
    }

    return -1;
}