module vk:tlas_mgr;

import <vulkan/vulkan_core.h>;
import :helpers;
import :device_extension_api;
import glm;

VkTransformMatrixKHR to_vk_transform(const Transform& t)
{
    glm::mat4 m = t.matrix();

    VkTransformMatrixKHR out{};

    out.matrix[0][0] = m[0][0];
    out.matrix[0][1] = m[1][0];
    out.matrix[0][2] = m[2][0];
    out.matrix[0][3] = m[3][0];

    out.matrix[1][0] = m[0][1];
    out.matrix[1][1] = m[1][1];
    out.matrix[1][2] = m[2][1];
    out.matrix[1][3] = m[3][1];

    out.matrix[2][0] = m[0][2];
    out.matrix[2][1] = m[1][2];
    out.matrix[2][2] = m[2][2];
    out.matrix[2][3] = m[3][2];

    return out;
}

RBAccelStruct vk::TLASManager::build_tlas(
    RBCommandList cmd, const std::vector<TLASInfo>& objects)
{
    std::vector<VkAccelerationStructureInstanceKHR> instances;
    instances.reserve(objects.size());

    for (auto& obj : objects)
    {
        
        const auto& mesh = mesh_manager.get_mesh_gpu_data(obj.mesh);

        VkAccelerationStructureInstanceKHR inst{};

        inst.transform = to_vk_transform(obj.transform);

        inst.instanceCustomIndex = obj.primitive_id;
        inst.mask = 0xFF;

        inst.instanceShaderBindingTableRecordOffset = 0;

        inst.flags =
            VK_GEOMETRY_INSTANCE_TRIANGLE_FACING_CULL_DISABLE_BIT_KHR;

        inst.accelerationStructureReference = mesh.blas_address;

        instances.push_back(inst);
    }
    
    VkDeviceSize size =
    sizeof(VkAccelerationStructureInstanceKHR) * instances.size();

    buffer_manager.create_device_local_buffer_with_data(
        instances.data(),
        size,
        VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR |
            VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT |
            VK_BUFFER_USAGE_TRANSFER_DST_BIT,
        tlas.instance_buffer,
        tlas.instance_memory
    );
    
    {
        VkBufferMemoryBarrier barrier{};
        barrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;

        barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

        barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;

        barrier.buffer = tlas.instance_buffer;
        barrier.offset = 0;
        barrier.size = VK_WHOLE_SIZE;

        vkCmdPipelineBarrier(
            cmd,
            VK_PIPELINE_STAGE_TRANSFER_BIT,
            VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR,
            0,
            0, nullptr,
            1, &barrier,
            0, nullptr
        );
    }
    
    VkDeviceAddress instance_address =
    buffer_manager.get_buffer_device_address(tlas.instance_buffer);

    VkAccelerationStructureGeometryInstancesDataKHR instances_data{
        VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_INSTANCES_DATA_KHR
    };

    instances_data.arrayOfPointers = VK_FALSE;
    instances_data.data.deviceAddress = instance_address;

    VkAccelerationStructureGeometryKHR geometry{
        VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR
    };

    geometry.geometryType = VK_GEOMETRY_TYPE_INSTANCES_KHR;
    geometry.geometry.instances = instances_data;
    
    VkAccelerationStructureBuildGeometryInfoKHR build_info{
        VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR
    };

    build_info.type = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR;
    build_info.flags = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR;
    build_info.geometryCount = 1;
    build_info.pGeometries = &geometry;
    build_info.mode = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;
    
    uint32_t primitive_count = instances.size();

    VkAccelerationStructureBuildSizesInfoKHR size_info{
        VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR
    };

    vk_ext::vkGetAccelerationStructureBuildSizesKHR(
        instance.device,
        VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR,
        &build_info,
        &primitive_count,
        &size_info
    );
    
    create_buffer(
        instance.device,
        instance.physical_device,
        size_info.accelerationStructureSize,
        VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR |
        VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
        tlas.buffer,
        tlas.memory
    );
    
    VkAccelerationStructureCreateInfoKHR create_info{
        VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR
    };

    create_info.type = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR;
    create_info.size = size_info.accelerationStructureSize;
    create_info.buffer = tlas.buffer;

    vk_ext::vkCreateAccelerationStructureKHR(
        instance.device,
        &create_info,
        nullptr,
        &tlas.tlas
    );
    
    VkBuffer scratch_buffer = VK_NULL_HANDLE;
    VkDeviceMemory scratch_memory = VK_NULL_HANDLE;
    VkDeviceAddress scratch_address = 0;
    
    create_buffer(
        instance.device,
        instance.physical_device,
        size_info.buildScratchSize,
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT |
        VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
        scratch_buffer,
        scratch_memory
    );

    scratch_address = buffer_manager.get_buffer_device_address(scratch_buffer);
    
    build_info.dstAccelerationStructure = tlas.tlas;
    build_info.scratchData.deviceAddress = scratch_address;

    VkAccelerationStructureBuildRangeInfoKHR range_info{};
    range_info.primitiveCount = primitive_count;
    range_info.primitiveOffset = 0;
    range_info.firstVertex = 0;
    range_info.transformOffset = 0;

    const VkAccelerationStructureBuildRangeInfoKHR* ranges[] = {
        &range_info
    };
    
   
    vk_ext::vkCmdBuildAccelerationStructuresKHR(
        cmd,
        1,
        &build_info,
        ranges
    );
    
    VkAccelerationStructureDeviceAddressInfoKHR addr{
        VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_DEVICE_ADDRESS_INFO_KHR
    };

    addr.accelerationStructure = tlas.tlas;

    tlas.address = vk_ext::vkGetAccelerationStructureDeviceAddressKHR(
        instance.device,
        &addr
    );
    return tlas.tlas;
}
