#pragma once
#include "object_reflection.h"
#include "math/transform.h"

class RhObject : public std::enable_shared_from_this<RhObject>
{
public:
    
    virtual ~RhObject() = default;
    
    virtual bool is_actor() const { return false; }
    
    void set_name(const std::string& in_name)
    {
        name = in_name;
    }
    
    void set_type_id(std::string_view in_type_id)
    {
        type_id = in_type_id;
    }
    
    std::string_view get_type_id() const
    {
        return type_id;
    }
    
    std::string_view type_id;
    std::string name;
};

REG_REFLECT(RhObject, void);