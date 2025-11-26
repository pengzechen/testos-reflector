#include <stdio.h>
#include <iostream>

class A
{
public:
    int x;
    int y;

public:
    A()  = default;
    ~A() = default;
};

class Base
{
public:
    virtual ~Base() {}  // 必须有虚函数，才会生成 RTTI 信息
};

class Derived : public Base
{
public:
    void
    hello()
    {
        std::cout << "Hello from Derived!\n";
    }
};

static int x = 100;

using namespace std;

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
    printf(" cin prt: %x\n", &std::cin);
    printf("cerr ptr: %x\n", &std::cerr);
    printf("clog ptr: %x\n", &std::clog);
    printf("cout ptr: %x\n", &std::cout);
    printf("cout.rdbuf ptr: %x\n", (void *) std::cout.rdbuf());
    printf(" cin.rdbuf ptr: %x\n", (void *) std::cin.rdbuf());
    printf("cerr.rdbuf ptr: %x\n", (void *) std::cerr.rdbuf());
    printf("clog.rdbuf ptr: %x\n", (void *) std::clog.rdbuf());
    cout << "Hello, C++ World!" << endl;


    Base *b = new Derived();

    // 测试 typeid
    std::cout << "typeid(*b).name() = " << typeid(*b).name() << std::endl;
    std::cout << "typeid(Derived).name() = " << typeid(Derived).name() << std::endl;

    // 测试 dynamic_cast 暂时用不了
    // Derived *d = dynamic_cast<Derived*>(b);
    // if (d)
    //     d->hello();
    // else
    //     std::cout << "dynamic_cast failed!\n";

    delete b;

    throw 1; // 只抛出一个异常是可以的。这个异常必须是基本类型。

    // 测试异常处理 暂时用不了
    // try {
    //     throw "error!";
    // } catch (const char *s) {
    //     cout << "caught: " << s << endl;
    // }

    return 0;
}