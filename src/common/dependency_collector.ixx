export module dependency_collector;

import <vector>;
import <future>;

export class DependencyCollector
{
public:
    
    void push(std::shared_future<void>&& op)
    {
        async_load_operations.push_back(std::move(op));
    }
    
    void wait() const
    {
        for (auto& op : async_load_operations)
            op.wait();
    }
    
    std::vector<std::shared_future<void>> async_load_operations;
};
