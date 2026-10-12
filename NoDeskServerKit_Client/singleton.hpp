#ifndef SINGLETON_H
#define SINGLETON_H
#include <QObject>
#include <global.h>
#include <mutex>

//模板类的声明与实现都要写在一起，避免模板实例化导致的重定义问题，文件名后缀以.hpp结尾，采用.h结尾也可
template <class T>//创建单例模式类模版，继承QObject类是为了保证单例子类可以使用信号槽机制
class Singleton: public QObject{
protected:
    Singleton()=default;//使用default保证子类可以使用，并且protected权限仅提供给子类使用
    Singleton(const Singleton<T>&)=delete;
    Singleton<T>& operator=(const Singleton<T>&)=delete;
    //使用智能指针shared_ptr自动释放内存，使用静态变量instance可以使单例模板类调用静态函数get_instance返回一个实例
    static std::shared_ptr<T> _instance;//创造一个抽象类T的静态智能指针instance

public:
    //获取一个只初始化一次的单例静态实例instance的静态公共函数
    static std::shared_ptr<T> get_instance(){
        //设置为静态std::once_flag变量flag_once只会初始化一次，使用call_once也只会调用一次回调函数初始化单例静态实例instance
        static std::once_flag flag_once;
        std::call_once(flag_once,[](){
            Singleton<T>::_instance=std::shared_ptr<T>(new T);//只能使用构造函数
        });
        return Singleton<T>::_instance;
    }

    //析构函数
    ~Singleton(){
        std::cout<<"单例静态实例instance被销毁了！"<<std::endl;
    }
};
//静态成员instance,类外必须初始化,类模板的静态变量在.hpp/.h文件类外进行初始化，同时也必须声明为模板
template<class T>
std::shared_ptr<T> Singleton<T>::_instance=nullptr;

#endif // SINGLETON_H
