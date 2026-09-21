#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// 기본 노드 구조체
typedef struct Node {
    int data;
    struct Node* next;
} Node;

// 리스트에 노드 추가 (맨 뒤에)
void add_node(Node** head, int data) {
    Node* new_node = (Node*)malloc(sizeof(Node));
    new_node->data = data;
    new_node->next = NULL;
    if (*head == NULL) {
        *head = new_node;
        return;
    }
    Node* cur = *head;
    while (cur->next) {
        cur = cur->next;
    }
    cur->next = new_node;
}

// 리스트에서 값이 data인 노드 삭제
void delete_node(Node** head, int data) {
    Node** cur = head;
    while (*cur) {
        if ((*cur)->data == data) {
            Node* to_delete = *cur;
            *cur = (*cur)->next;
            free(to_delete);
            return;
        }
        cur = &((*cur)->next);
    }
}

// 리스트 전체 출력 (테스트용)
void print_list(const Node* head) {
    const Node* cur = head;
    printf("List: ");
    while (cur) {
        printf("%d ", cur->data);
        cur = cur->next;
    }
    printf("\n");
}

// 리스트를 배열로 복사 (테스트용)
void list_to_array(const Node* head, int* arr, int* len) {
    int i = 0;
    while (head) {
        arr[i++] = head->data;
        head = head->next;
    }
    *len = i;
}

// 리스트 초기화 (head를 NULL로)
void init_list(Node** head) {
    *head = NULL;
}

// 리스트 메모리 해제
void free_list(Node* head) {
    Node* cur = head;
    while (cur) {
        Node* next = cur->next;
        free(cur);
        cur = next;
    }
}

// 리스트 역순(reverse)
void reverse_list(Node** head) {
    Node* prev = NULL;
    Node* curr = *head;
    while (curr) {
        Node* next = curr->next;
        curr->next = prev;
        prev = curr;
        curr = next;
    }
    *head = prev;
}

// 배열 비교 (테스트용)
int expect_list(const Node* head, const int* expect, int n) {
    int arr[100], len = 0;
    list_to_array(head, arr, &len);
    if (len != n) return 0;
    for (int i = 0; i < n; ++i)
        if (arr[i] != expect[i]) return 0;
    return 1;
}

// 테스트 코드
int main() {
    Node* head;
    init_list(&head);

    int pass = 0, total = 0;

    printf("Case 1: Add 10\n");
    add_node(&head, 10);
    print_list(head);
    int expect1[] = {10};
    printf("Case 1 %s\n", (expect_list(head, expect1, 1) ? "PASS" : "FAIL"));
    pass += expect_list(head, expect1, 1); total++;

    printf("Case 2: Add 20, 30\n");
    add_node(&head, 20);
    add_node(&head, 30);
    print_list(head);
    int expect2[] = {10, 20, 30};
    printf("Case 2 %s\n", (expect_list(head, expect2, 3) ? "PASS" : "FAIL"));
    pass += expect_list(head, expect2, 3); total++;

    printf("Case 3: Delete 20\n");
    delete_node(&head, 20);
    print_list(head);
    int expect3[] = {10, 30};
    printf("Case 3 %s\n", (expect_list(head, expect3, 2) ? "PASS" : "FAIL"));
    pass += expect_list(head, expect3, 2); total++;

    printf("Case 4: Delete 10\n");
    delete_node(&head, 10);
    print_list(head);
    int expect4[] = {30};
    printf("Case 4 %s\n", (expect_list(head, expect4, 1) ? "PASS" : "FAIL"));
    pass += expect_list(head, expect4, 1); total++;

    printf("Case 5: Delete 99 (not exist)\n");
    delete_node(&head, 99);
    print_list(head);
    int expect5[] = {30};
    printf("Case 5 %s\n", (expect_list(head, expect5, 1) ? "PASS" : "FAIL"));
    pass += expect_list(head, expect5, 1); total++;

    printf("Case 6: Reverse list\n");
    reverse_list(&head);
    print_list(head);
    int expect6[] = {30};
    printf("Case 6 %s\n", (expect_list(head, expect6, 1) ? "PASS" : "FAIL"));
    pass += expect_list(head, expect6, 1); total++;

    free_list(head);

    printf("Total: %d/%d PASS\n", pass, total);
    return 0;
}