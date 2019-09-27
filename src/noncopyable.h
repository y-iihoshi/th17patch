
#pragma once

template <typename T>
class noncopyable
{
public:
	noncopyable() = default;
	noncopyable(const noncopyable&) = delete;
	noncopyable& operator=(const noncopyable&) = delete;
};
