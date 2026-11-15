/* expect: link — declared (so the compiler is happy) but defined nowhere */
int flash_erase(unsigned block);

int main(void)
{
    return flash_erase(7u);
}
