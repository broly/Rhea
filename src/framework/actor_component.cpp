module framework;

import :rhcomponent;

import std.compat;


void RhComponent::on_add(std::shared_ptr<RhActor> actor)
{
    owner = actor;
}

void RhComponent::start()
{
}

void RhComponent::finish()
{

}

void RhComponent::tick(double dt)
{
}

void RhComponent::set_owner(std::shared_ptr<RhActor> actor)
{
    owner = actor;
}
