module rhcomponents;

import :sky;

import std.compat;
import glm;

// CPU side of the sky model: the same atmosphere as shaders/sky/atmosphere.glsl (keep them in sync), used
// where a few values are needed outside of a shader: the color of the sun light at the ground
// (SkyController), the sky light on the clouds and the ambient of surfaces no reflection probe covers
// (SkyRenderer).

namespace
{
    constexpr double pi = 3.14159265358979323846;
    constexpr double ozone_center = 25000.0;
    constexpr double ozone_half_width = 15000.0;
    constexpr int view_steps = 16;
    constexpr int light_steps = 8;

    // Earth, per meter
    constexpr glm::dvec3 rayleigh_reference(33.1e-6);           // x SkyAtmosphere::rayleigh_color
    constexpr double mie_reference = 3.996e-6;
    constexpr double mie_single_scattering_albedo = 0.9;
    constexpr glm::dvec3 ozone_reference(0.650e-6, 1.881e-6, 0.085e-6);
    constexpr double atmosphere_height = 60000.0;

    struct Medium
    {
        glm::dvec3 rayleigh;
        glm::dvec3 mie;
        glm::dvec3 mie_extinction;
        glm::dvec3 ozone;
        double rayleigh_height;
        double mie_height;
        double rg;
        double rt;

        explicit Medium(const AtmosphereModel& model)
            : rayleigh(model.rayleigh_scattering)
            , mie(model.mie_scattering)
            , mie_extinction(model.mie_extinction)
            , ozone(model.ozone_absorption)
            , rayleigh_height(model.rayleigh_height)
            , mie_height(model.mie_height)
            , rg(model.planet_radius)
            , rt(model.atmosphere_radius)
        {}

        // altitude of a point with |p|^2 - rg^2 = q
        double altitude(double q) const { return q / (std::sqrt(rg * rg + q) + rg); }

        // x: air, y: haze, z: ozone
        glm::dvec3 density(double altitude) const
        {
            const double h = std::max(altitude, 0.0);
            return {
                std::exp(-h / rayleigh_height),
                std::exp(-h / mie_height),
                std::max(0.0, 1.0 - std::abs(h - ozone_center) / ozone_half_width),
            };
        }

        glm::dvec3 extinction(const glm::dvec3& d) const
        {
            return rayleigh * d.x + mie_extinction * d.y + ozone * d.z;
        }

        double distance_to_top(double altitude, double mu) const
        {
            const double r = rg + altitude;
            const double b = r * mu;
            const double k = std::max((rt - r) * (rt + r), 0.0);
            const double s = std::sqrt(b * b + k);
            return b > 0.0 ? k / (b + s) : s - b;
        }

        glm::dvec3 transmittance(double altitude, double mu) const
        {
            const double r = rg + altitude;
            const double b = r * mu;
            const double c_ground = altitude * (2.0 * rg + altitude);
            if (mu < 0.0 && b * b - c_ground > 0.0)
                return glm::dvec3(0.0);

            const double t_top = distance_to_top(altitude, mu);
            glm::dvec3 tau(0.0);
            for (int i = 0; i < light_steps; ++i)
            {
                const double a0 = double(i) / light_steps;
                const double a1 = double(i + 1) / light_steps;
                const double u0 = t_top * a0 * a0;
                const double u1 = t_top * a1 * a1;
                const double u = 0.5 * (u0 + u1);
                tau += extinction(density(this->altitude(c_ground + u * (2.0 * b + u)))) * (u1 - u0);
            }
            return glm::exp(-tau);
        }

        // the same straight up, in closed form
        glm::dvec3 transmittance_zenith(double altitude) const
        {
            const double h = std::max(altitude, 0.0);
            const double x = std::clamp(h, ozone_center - ozone_half_width, ozone_center + ozone_half_width);
            const double below = std::max(ozone_center - x, 0.0);
            const double above = ozone_half_width - std::max(x - ozone_center, 0.0);
            const double ozone_column = below - below * below / (2.0 * ozone_half_width)
                + above * above / (2.0 * ozone_half_width);
            return glm::exp(-(rayleigh * (rayleigh_height * std::exp(-h / rayleigh_height))
                + mie_extinction * (mie_height * std::exp(-h / mie_height))
                + ozone * ozone_column));
        }
    };

    // light scattered more than once fades through the twilight
    double multiple_scattering_at(double mu_sun)
    {
        const double twilight = glm::smoothstep(-0.28, 0.05, mu_sun);
        return twilight * twilight;
    }

    double phase_rayleigh(double cos_theta)
    {
        return 3.0 / (16.0 * pi) * (1.0 + cos_theta * cos_theta);
    }

    double phase_hg(double cos_theta, double g)
    {
        const double g2 = g * g;
        return (1.0 - g2) / (4.0 * pi * std::pow(std::max(1.0 + g2 - 2.0 * g * cos_theta, 1e-4), 1.5));
    }
}

AtmosphereModel AtmosphereModel::from(const SkyAtmosphere& sky)
{
    AtmosphereModel model;
    model.rayleigh_scattering = glm::vec3(rayleigh_reference * glm::dvec3(sky.rayleigh_color.glm()) * double(sky.rayleigh));
    model.mie_scattering = glm::vec3(float(mie_reference * sky.mie));
    model.mie_extinction = model.mie_scattering / float(mie_single_scattering_albedo);
    model.mie_anisotropy = std::clamp(sky.mie_anisotropy, 0.0f, 0.99f);
    model.ozone_absorption = glm::vec3(ozone_reference * double(sky.ozone));
    model.multiple_scattering = std::max(sky.multiple_scattering, 0.0f);
    model.planet_radius = std::max(sky.planet_radius, 100.0f) * 1000.0f;
    model.atmosphere_radius = model.planet_radius + float(atmosphere_height);
    model.ground_altitude = std::max(sky.ground_altitude, 0.0f);
    model.intensity = std::max(sky.intensity, 0.0f);
    model.ground_albedo = glm::clamp(sky.ground_color.glm(), glm::vec3(0.0f), glm::vec3(1.0f));
    return model;
}

glm::vec3 AtmosphereModel::transmittance(float world_y, const glm::vec3& direction) const
{
    const Medium medium(*this);
    const double altitude = std::max(double(world_y) + ground_altitude, 1.0);
    return glm::vec3(medium.transmittance(altitude, glm::normalize(glm::dvec3(direction)).y));
}

glm::vec3 AtmosphereModel::radiance(float world_y, const glm::vec3& direction, const glm::vec3& sun_direction,
    const glm::vec3& sun_illuminance) const
{
    const Medium medium(*this);
    const glm::dvec3 dir = glm::normalize(glm::dvec3(direction));
    const glm::dvec3 sun = glm::normalize(glm::dvec3(sun_direction));

    const double rg = medium.rg;
    const double h0 = std::max(double(world_y) + ground_altitude, 1.0);
    const double r0 = rg + h0;
    const double b = r0 * dir.y;
    const double c_ground = h0 * (2.0 * rg + h0);

    double t_max = medium.distance_to_top(h0, dir.y);
    bool hits_ground = false;
    const double disc = b * b - c_ground;
    if (dir.y < 0.0 && disc > 0.0)
    {
        t_max = c_ground / (std::sqrt(disc) - b);
        hits_ground = true;
    }

    const double cos_theta = glm::dot(dir, sun);
    const double multiple = multiple_scattering / (4.0 * pi);
    const double phase_r = phase_rayleigh(cos_theta);
    const double phase_m = phase_hg(cos_theta, mie_anisotropy);

    glm::dvec3 tau(0.0);
    glm::dvec3 result(0.0);
    for (int i = 0; i < view_steps; ++i)
    {
        const double a0 = double(i) / view_steps;
        const double a1 = double(i + 1) / view_steps;
        const double t0 = t_max * a0 * a0;
        const double t1 = t_max * a1 * a1;
        const double t = 0.5 * (t0 + t1);
        const double dt = t1 - t0;

        const double altitude = medium.altitude(c_ground + t * (2.0 * b + t));
        const glm::dvec3 density = medium.density(altitude);
        const glm::dvec3 extinction = medium.extinction(density);
        const glm::dvec3 view_transmittance = glm::exp(-(tau + extinction * (0.5 * dt)));
        tau += extinction * dt;

        const double mu_sun = std::clamp((r0 * sun.y + t * cos_theta) / (rg + altitude), -1.0, 1.0);
        const glm::dvec3 sun_transmittance = medium.transmittance(altitude, mu_sun);

        const glm::dvec3 rayleigh = medium.rayleigh * density.x;
        const glm::dvec3 mie = medium.mie * density.y;
        result += view_transmittance * dt * (
            sun_transmittance * (rayleigh * phase_r + mie * phase_m)
            + medium.transmittance_zenith(altitude) * (multiple_scattering_at(mu_sun) * multiple) * (rayleigh + mie));
    }
    result *= glm::dvec3(sun_illuminance);

    if (hits_ground)
    {
        const glm::dvec3 normal = glm::normalize(glm::dvec3(0.0, r0, 0.0) + dir * t_max);
        const double mu_sun = glm::dot(normal, sun);
        const glm::dvec3 irradiance = glm::dvec3(sun_illuminance) * medium.transmittance(0.0, mu_sun) * std::max(mu_sun, 0.0);
        result += glm::exp(-tau) * glm::dvec3(ground_albedo) / pi * irradiance;
    }

    return glm::vec3(result * double(intensity));
}
