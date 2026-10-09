#include "network/player_animation_sync.h"
#include <cstdlib>
#include <limits>
#include <cstdio>
using namespace PlayerAnimation;
static unsigned checks=0;
void expect(bool ok){++checks;if(!ok){std::printf("Failure %u\n",checks);std::exit(1);}}
int main()
{
    OwnerClock clock; expect(!clock.Next()); expect(clock.NewBirth()); expect(clock.birth == 1 && clock.sequence == 1);
    expect(clock.Next()); expect(clock.NewBirth()); expect(clock.birth == 2 && clock.sequence == 3);
    OwnerClock exhausted{MAX_COUNTER, 3}; expect(!exhausted.NewBirth()); expect(exhausted.sequence == 3);
    Cache cache; Life life{5,1,1,0,0,42}; State idle{1,1,3,1,1,false,1};
    expect(cache.Accept(life,idle,100)); expect(!cache.Accept(life,idle,101));
    life.birth=2; life.sequence=2; expect(cache.Accept(life,{},101));
    life.birth=1; life.sequence=3; expect(!cache.Accept(life,idle,102));
    life.birth=2; life.generation=6; expect(!cache.Accept(life,idle,102));
    float phase=0; expect(Phase(idle,100,1100,phase) && phase == 2);
    expect(!Phase(idle,100,2100,phase)); idle.loop=true; expect(Phase(idle,100,3100,phase) && phase == 1);
    idle.phase=std::numeric_limits<float>::quiet_NaN(); expect(!idle.Valid());
    for(int group=0;group<100;group++) for(int anim=0;anim<1000;anim++)
    { int p=Pose(group,anim); expect(!p || (Group(p)==group && Animation(p)==anim)); }
    std::printf("%u contract assertions passed\n",checks);
}
