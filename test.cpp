#include <iostream>

using namespace std;

int main()
{
    ssize_t s  = 10;
    char buf[] = "new";
    buf+=s;
    cout<<buf<<"\n";
}