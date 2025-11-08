#include <stdio.h>

class A {
public:
    int x;
    int y;
public:
    A() = default;
    ~A() = default;
};

static int x = 100;

int
main()
{
    printf("before new!\n");
    A *a = new A();
    a->x = 10;
    a->y = 20;
    printf("A.x = %d, A.y = %d\n", a->x, a->y);
    printf("global x = %d\n", x);
    delete a;
    printf("after delete!\n");
    return 0;
}