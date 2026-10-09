#include "../shared/config.h"
#include "../shared/semver.h"

int main()
{
    const auto current = semver_parse(COOPANDREAS_VERSION, nullptr);
    const auto previous = semver_parse("0.3.0-alpha", nullptr);
    semver_t decoded{};
    semver_unpack(current, &decoded);
    if (!current || current == previous || decoded.patch != 1 || decoded.stage != SEMVER_STAGE_ALPHA)
        return 1;
    printf("Current protocol: %s (%u); previous: 0.3.0-alpha (%u). Existing exact-version check rejects the previous build.\n",
        COOPANDREAS_VERSION, current, previous);
}
