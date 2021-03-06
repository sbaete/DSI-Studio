#ifndef linear_hpp
#define linear_hpp
#include <boost/lambda/lambda.hpp>
#include <boost/thread/thread.hpp>
#include "image/image.hpp"

template<typename ImageType>
class LinearMapping
{
    static const int dim = ImageType::dimension;

public:
    ImageType from,to;
    std::auto_ptr<boost::thread> thread;
    image::affine_transform<dim> arg_min;
    bool terminated;
    bool ended;
public:
    mutable image::transformation_matrix<dim,float> result;
public:

    LinearMapping(void):terminated(true),ended(true) {}
    ~LinearMapping(void)
    {
        terminate();
    }

    void terminate(void)
    {
        terminated = true;
        if(thread.get())
        {
            thread->joinable();
            thread->join();
        }
        ended = true;
    }

    void argmin(int reg_type)
    {
        terminated = false;
        ended = false;
        image::reg::linear(from,to,arg_min,reg_type,image::reg::mutual_information(),terminated);
    }

    const float* get(void) const
    {
        image::transformation_matrix<dim,float> T(arg_min,from.geometry(),to.geometry());
        result = T;
        return result.get();
    }
    void thread_argmin(int reg_type)
    {
        thread.reset(new boost::thread(&LinearMapping::argmin,this,reg_type));
    }
};
// dimenion = 3
typedef  LinearMapping<image::pointer_image<float,3> > lm3_type;

#endif//linear_hpp
