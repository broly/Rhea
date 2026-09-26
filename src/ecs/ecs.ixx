// Entity Component System: entities are handles, components are plain structs stored in archetype tables,
// systems are free functions whose parameters (Query / Commands / Res / ResMut) declare what they access.
export module ecs;

export import :entity;
export import :component;
export import :registry;
export import :query;
export import :commands;
export import :schedule;
