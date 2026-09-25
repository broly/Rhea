module;

#include <imgui.h>
#include <imgui_stdlib.h>

module ui;

import std.compat;
import properties;
import reflect;
import type_id;
import name;
import glm;
import rhmath;


namespace
{
    template<typename T>
    constexpr ImGuiDataType data_type_of()
    {
        if constexpr (std::is_same_v<T, float>) return ImGuiDataType_Float;
        else if constexpr (std::is_same_v<T, double>) return ImGuiDataType_Double;
        else if constexpr (std::is_signed_v<T> && sizeof(T) == 1) return ImGuiDataType_S8;
        else if constexpr (std::is_signed_v<T> && sizeof(T) == 2) return ImGuiDataType_S16;
        else if constexpr (std::is_signed_v<T> && sizeof(T) == 4) return ImGuiDataType_S32;
        else if constexpr (std::is_signed_v<T> && sizeof(T) == 8) return ImGuiDataType_S64;
        else if constexpr (sizeof(T) == 1) return ImGuiDataType_U8;
        else if constexpr (sizeof(T) == 2) return ImGuiDataType_U16;
        else if constexpr (sizeof(T) == 4) return ImGuiDataType_U32;
        else return ImGuiDataType_U64;
    }

    float drag_speed(const reflect::PropertyMeta& meta, float fallback)
    {
        if (meta.speed > 0.0f)
            return meta.speed;
        if (meta.has_range)
            return std::max((meta.max - meta.min) / 500.0f, 0.0001f);
        return fallback;
    }

    template<typename T>
    bool edit_integer(const char* label, void* value, const reflect::PropertyMeta& meta)
    {
        T* typed = static_cast<T*>(value);
        if (meta.has_range)
        {
            const T min = (T)meta.min;
            const T max = (T)meta.max;
            return ImGui::SliderScalar(label, data_type_of<T>(), typed, &min, &max);
        }
        return ImGui::DragScalar(label, data_type_of<T>(), typed, drag_speed(meta, 0.2f));
    }

    template<typename T>
    bool edit_floating(const char* label, void* value, const reflect::PropertyMeta& meta)
    {
        T* typed = static_cast<T*>(value);
        if (meta.degrees)
        {
            // stored in radians, range (if any) in degrees
            float degrees = glm::degrees((float)*typed);
            const bool changed = meta.has_range
                ? ImGui::SliderFloat(label, &degrees, meta.min, meta.max, "%.1f deg")
                : ImGui::DragFloat(label, &degrees, drag_speed(meta, 0.5f), 0.0f, 0.0f, "%.1f deg");
            if (changed)
                *typed = (T)glm::radians(degrees);
            return changed;
        }
        if (meta.has_range)
        {
            const T min = (T)meta.min;
            const T max = (T)meta.max;
            return ImGui::SliderScalar(label, data_type_of<T>(), typed, &min, &max, "%.3f");
        }
        return ImGui::DragScalar(label, data_type_of<T>(), typed, drag_speed(meta, 0.01f), nullptr, nullptr, "%.3f");
    }

    bool edit_bool(const char* label, void* value, const reflect::PropertyMeta&)
    {
        return ImGui::Checkbox(label, static_cast<bool*>(value));
    }

    bool edit_string(const char* label, void* value, const reflect::PropertyMeta&)
    {
        return ImGui::InputText(label, static_cast<std::string*>(value));
    }

    bool edit_name(const char* label, void* value, const reflect::PropertyMeta&)
    {
        // Names are interned: commit on Enter only
        Name* name = static_cast<Name*>(value);
        std::string text = name->to_string();
        if (ImGui::InputText(label, &text, ImGuiInputTextFlags_EnterReturnsTrue))
        {
            *name = Name(text);
            return true;
        }
        return false;
    }

    constexpr ImGuiColorEditFlags color_flags = ImGuiColorEditFlags_Float | ImGuiColorEditFlags_HDR;

    // N consecutive floats (glm vectors, rhmath vec2/3/4)
    template<int N>
    bool edit_float_n(const char* label, float* values, const reflect::PropertyMeta& meta)
    {
        if constexpr (N == 3)
            if (meta.color)
                return ImGui::ColorEdit3(label, values, color_flags);
        if constexpr (N == 4)
            if (meta.color)
                return ImGui::ColorEdit4(label, values, color_flags);

        const float min = meta.has_range ? meta.min : 0.0f;
        const float max = meta.has_range ? meta.max : 0.0f;
        return ImGui::DragScalarN(label, ImGuiDataType_Float, values, N, drag_speed(meta, 0.01f),
            meta.has_range ? &min : nullptr, meta.has_range ? &max : nullptr, "%.3f");
    }

    template<typename V, int N>
    bool edit_vector(const char* label, void* value, const reflect::PropertyMeta& meta)
    {
        static_assert(sizeof(V) == sizeof(float) * N);
        return edit_float_n<N>(label, reinterpret_cast<float*>(value), meta);
    }

    // rotation as euler angles in degrees
    bool edit_rotation(const char* label, glm::quat& rotation)
    {
        glm::vec3 euler = glm::degrees(glm::eulerAngles(rotation));
        if (ImGui::DragFloat3(label, &euler.x, 0.5f, 0.0f, 0.0f, "%.1f deg"))
        {
            rotation = glm::quat(glm::radians(euler));
            return true;
        }
        return false;
    }

    bool edit_glm_quat(const char* label, void* value, const reflect::PropertyMeta&)
    {
        return edit_rotation(label, *static_cast<glm::quat*>(value));
    }

    bool edit_rh_quat(const char* label, void* value, const reflect::PropertyMeta&)
    {
        quat& q = *static_cast<quat*>(value);
        glm::quat rotation = q.glm();
        if (!edit_rotation(label, rotation))
            return false;
        q = rotation;
        return true;
    }

    bool edit_transform(const char* label, void* value, const reflect::PropertyMeta&)
    {
        Transform& t = *static_cast<Transform*>(value);
        bool changed = false;

        ImGui::PushID(label);
        const float width = ImGui::CalcItemWidth();
        const float prefix = ImGui::CalcTextSize("P ").x;
        auto row = [&] (const char* prefix_text, auto&& widget) {
            ImGui::AlignTextToFramePadding();
            ImGui::TextUnformatted(prefix_text);
            ImGui::SameLine(0.0f, 0.0f);
            ImGui::SetNextItemWidth(width - prefix);
            changed |= widget();
        };
        row("P ", [&] { return ImGui::DragFloat3("##position", &t.position.x, 0.01f, 0.0f, 0.0f, "%.3f"); });
        row("R ", [&] {
            glm::quat rotation = t.rotation.glm();
            if (!edit_rotation("##rotation", rotation))
                return false;
            t.rotation = rotation;
            return true;
        });
        row("S ", [&] { return ImGui::DragFloat3("##scale", &t.scale.x, 0.01f, 0.0f, 0.0f, "%.3f"); });
        ImGui::PopID();
        return changed;
    }

    bool edit_enum(const char* label, const reflect::EnumInfo& info, void* value)
    {
        const int64_t current = info.get(value);
        const int32_t current_index = info.find_index(current);
        const std::string preview = current_index >= 0
            ? std::string(info.names[current_index])
            : std::to_string(current);

        bool changed = false;
        if (ImGui::BeginCombo(label, preview.c_str()))
        {
            for (size_t index = 0; index < info.names.size(); index++)
            {
                const bool selected = (int32_t)index == current_index;
                const std::string item(info.names[index]);
                if (ImGui::Selectable(item.c_str(), selected) && !selected)
                {
                    info.set(value, info.values[index]);
                    changed = true;
                }
                if (selected)
                    ImGui::SetItemDefaultFocus();
            }
            ImGui::EndCombo();
        }
        return changed;
    }

    std::unordered_map<TypeId, ui::ValueEditor>& get_editors()
    {
        static std::unordered_map<TypeId, ui::ValueEditor> editors = [] {
            std::unordered_map<TypeId, ui::ValueEditor> result;
            result[get_type_id<bool>()] = &edit_bool;
            result[get_type_id<signed char>()] = &edit_integer<signed char>;
            result[get_type_id<unsigned char>()] = &edit_integer<unsigned char>;
            result[get_type_id<char>()] = &edit_integer<char>;
            result[get_type_id<short>()] = &edit_integer<short>;
            result[get_type_id<unsigned short>()] = &edit_integer<unsigned short>;
            result[get_type_id<int>()] = &edit_integer<int>;
            result[get_type_id<unsigned int>()] = &edit_integer<unsigned int>;
            result[get_type_id<long>()] = &edit_integer<long>;
            result[get_type_id<unsigned long>()] = &edit_integer<unsigned long>;
            result[get_type_id<long long>()] = &edit_integer<long long>;
            result[get_type_id<unsigned long long>()] = &edit_integer<unsigned long long>;
            result[get_type_id<float>()] = &edit_floating<float>;
            result[get_type_id<double>()] = &edit_floating<double>;
            result[get_type_id<std::string>()] = &edit_string;
            result[get_type_id<Name>()] = &edit_name;
            result[get_type_id<glm::vec2>()] = &edit_vector<glm::vec2, 2>;
            result[get_type_id<glm::vec3>()] = &edit_vector<glm::vec3, 3>;
            result[get_type_id<glm::vec4>()] = &edit_vector<glm::vec4, 4>;
            result[get_type_id<glm::quat>()] = &edit_glm_quat;
            result[get_type_id<vec2>()] = &edit_vector<vec2, 2>;
            result[get_type_id<vec3>()] = &edit_vector<vec3, 3>;
            result[get_type_id<vec4>()] = &edit_vector<vec4, 4>;
            result[get_type_id<quat>()] = &edit_rh_quat;
            result[get_type_id<Transform>()] = &edit_transform;
            return result;
        }();
        return editors;
    }

    ui::ValueEditor find_editor(TypeId type)
    {
        auto& editors = get_editors();
        auto it = editors.find(type);
        return it != editors.end() ? it->second : nullptr;
    }

    bool has_nested_rows(const reflect::PropertyDesc& property)
    {
        return !find_editor(property.type) && !property.enum_info && property.fields && !property.fields->empty();
    }
}

void ui::register_value_editor(TypeId type, ValueEditor editor)
{
    get_editors()[type] = editor;
}

bool ui::edit_value(const reflect::PropertyDesc& property, void* value, const char* label)
{
    bool changed = false;
    ImGui::BeginDisabled(property.meta.read_only);
    if (ValueEditor editor = find_editor(property.type))
        changed = editor(label, value, property.meta);
    else if (property.enum_info)
        changed = edit_enum(label, *property.enum_info, value);
    else if (property.fields && !property.fields->empty())
        ImGui::TextDisabled("{ %zu fields }", property.fields->size());
    else
        ImGui::TextDisabled("<%s>", property.type.name.to_string().c_str());
    ImGui::EndDisabled();
    return changed;
}

bool ui::begin_property_table(const char* table_id)
{
    const ImGuiTableFlags flags = ImGuiTableFlags_Resizable | ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_RowBg;
    if (!ImGui::BeginTable(table_id, 2, flags))
        return false;
    ImGui::TableSetupColumn("Property", ImGuiTableColumnFlags_WidthStretch, 0.4f);
    ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch, 0.6f);
    return true;
}

void ui::end_property_table()
{
    ImGui::EndTable();
}

bool ui::edit_property_row(const reflect::PropertyDesc& property, void* value)
{
    const std::string name(property.name);
    bool changed = false;

    ImGui::TableNextRow();
    ImGui::TableSetColumnIndex(0);
    ImGui::PushID(name.c_str());

    if (has_nested_rows(property))
    {
        const bool open = ImGui::TreeNodeEx(name.c_str(), ImGuiTreeNodeFlags_SpanFullWidth);
        ImGui::TableSetColumnIndex(1);
        ImGui::TextDisabled("%s", property.type.name.to_string().c_str());
        if (open)
        {
            ImGui::BeginDisabled(property.meta.read_only);
            for (const reflect::PropertyDesc& field : *property.fields)
                changed |= edit_property_row(field, field.value_ptr(value));
            ImGui::EndDisabled();
            ImGui::TreePop();
        }
    }
    else
    {
        ImGui::AlignTextToFramePadding();
        ImGui::TreeNodeEx(name.c_str(),
            ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen | ImGuiTreeNodeFlags_SpanFullWidth);
        ImGui::TableSetColumnIndex(1);
        ImGui::SetNextItemWidth(-FLT_MIN);
        changed = edit_value(property, value, "##value");
    }

    ImGui::PopID();
    return changed;
}

bool ui::edit_properties(const reflect::PropertyObject& object, std::vector<std::string_view>* changed,
    const char* table_id)
{
    if (object.empty())
    {
        ImGui::TextDisabled("No editable properties");
        return false;
    }

    bool any_changed = false;
    if (begin_property_table(table_id))
    {
        for (const reflect::PropertyDesc& property : *object.properties)
        {
            if (edit_property_row(property, property.value_ptr(object.object)))
            {
                any_changed = true;
                if (changed)
                    changed->push_back(property.name);
            }
        }
        end_property_table();
    }
    return any_changed;
}
