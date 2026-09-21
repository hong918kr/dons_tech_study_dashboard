typedef struct node {
    int data;
    struct node *next;
} node_t;

node_t* node_create(int data)
{
    node_t* newNode = malloc(sizeof(node_t));
    if (newNode == NULL) return NULL;
    newNode->data = data;
    newNode->next = NULL;
    return newNode;
}

void insert_head (node_t** head, int data)
{
    node_t* newNode = node_create(data);
    if (newNode == NULL) return NULL;
    newNode->data = data;
    newNode->next = NULL;
    return newNode;
}

void push_back (node_t)