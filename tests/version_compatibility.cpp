#include "../shared/config.h"
#include "../shared/semver.h"

int main()
{
    const auto current = semver_parse(COOPANDREAS_VERSION, nullptr);
    const auto previous = semver_parse("0.7.1-alpha", nullptr);
    const auto previousWorld = semver_parse("0.5.0-alpha", nullptr);
    const auto previousVitals = semver_parse("0.4.0-alpha", nullptr);
    const auto previousSession = semver_parse("0.3.3-alpha", nullptr);
    const auto previousVehicle = semver_parse("0.3.2-alpha", nullptr);
    const auto earlier = semver_parse("0.3.1-alpha", nullptr);
    const auto original = semver_parse("0.3.0-alpha", nullptr);
    semver_t decoded{};
    semver_unpack(current, &decoded);
    if (!current || current == previous || current == previousWorld || current == previousVitals || current == previousSession || current == previousVehicle || current == earlier || current == original
        || decoded.major != 0 || decoded.minor != 8 || decoded.patch != 0 || decoded.stage != SEMVER_STAGE_ALPHA)
        return 1;
    printf("Current protocol: %s (%u); previous: 0.7.1-alpha (%u). Existing exact-version check rejects the previous build.\n",
        COOPANDREAS_VERSION, current, previous);
}
