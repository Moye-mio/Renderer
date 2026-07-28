#pragma once
#include <memory>

template <typename T>
class Singleton
{
public:
    virtual ~Singleton() = default;

    static std::shared_ptr<T> GetOrCreateInstance()
    {
        if (!m_instance)
            m_instance.reset(new T()); //Can not use std::make_shared<T>(), because private constructor of T cant be accessed in memory(shared_ptr object) 
        return m_instance;
    }

protected: //protected, not private, because T's constructor will call this CSingleton() constructor 
    Singleton() = default;

private:
    static std::shared_ptr<T> m_instance;
};

template <typename T>
std::shared_ptr<T> Singleton<T>::m_instance = nullptr;
