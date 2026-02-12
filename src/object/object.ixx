export module rhobject:object;
import dependency_collector;

import <memory>;
import <string>;
import <optional>;
import static_name;
import name;




export struct ObjectInitData
{
    Name name;
    bool is_default = false;
};


export class RhObject : public std::enable_shared_from_this<RhObject>
{
public:
    
    virtual ~RhObject() {};
    
    virtual bool is_actor() const { return false; }
    
    void init(Name type_name, const ObjectInitData& init_data)
    {
        set_type_name(type_name);
        set_name(init_data.name);
        unique_id = next_id(this);
        is_default = init_data.is_default;
        on_init();
    }
    
    virtual void on_init() {}
    
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
    
    static uint32_t next_id(RhObject* obj);
    
    virtual void on_serialize(DependencyCollector* dc) {}
    
    Name type_name;
    Name name;
    uint32_t unique_id;
    bool is_default;
};

uint32_t RhObject::next_id(RhObject* obj)
{
    static uint32_t id_counter = 0;
    
    return id_counter++;
}

