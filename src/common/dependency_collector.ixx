export module dependency_collector;

import <vector>;
import <future>;

export class DependencyCollector
{
public:
    
    void push(std::future<void>&& op)
    {
        async_load_operations.push_back(std::move(op));
    }
    
    void wait() const
    {
        for (auto& op : async_load_operations)
            op.wait();
    }
    
    std::vector<std::future<void>> async_load_operations;
};
