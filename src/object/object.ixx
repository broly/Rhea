export module rhobject:object;
import dependency_collector;

import <memory>;
import <string>;
import <optional>;
import static_name;
import name;

export class RhObject : public std::enable_shared_from_this<RhObject>
{
public:
    
    virtual ~RhObject() = default;
    
    virtual bool is_actor() const { return false; }
    
    void set_name(Name in_name)
    {
        name = in_name;
    }
    
    void set_type_name(Name in_type_id)
    {
        type_name = in_type_id;
    }
    
    Name get_type_name() const
    {
        return type_name;
    }
    
    virtual void on_serialize(DependencyCollector* dc) {}
    
    Name type_name;
    Name name;
};

