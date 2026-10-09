#include <iostream>
#include "network/trailer_sync.h"
using namespace TrailerSync;
int main(){int checks=0,failures=0;auto test=[&](bool ok){++checks;if(!ok)++failures;};
Cache c;test(c.Reset(1));Lease a{{0,1,403},1,0,true},b{{1,2,435},1,1,true};test(c.Bind(a));test(c.Bind(b));
Link l;l.room=1;l.parent=a.vehicle;l.child=b.vehicle;l.ownerEpoch=1;l.sequence=1;l.attached=true;l.revision=1;
test(c.Authorized(l,0));test(!c.Authorized(l,1));test(c.State(l));test(c.Linked(1,2));test(!c.State(l));
l.sequence=2;test(c.Position(l));test(!c.Position(l));l.revision=2;l.sequence=4;test(c.Position(l));
Link detached=l;detached.attached=false;detached.sequence=1;test(c.State(detached));test(!c.Position(l));test(!c.Linked(1,2));
a.epoch=2;a.owner=1;test(c.Bind(a));l.ownerEpoch=1;test(!c.Authorized(l,0));l.ownerEpoch=2;test(c.Authorized(l,1));
a.live=false;test(c.Bind(a));a.live=true;test(!c.Bind(a));a.vehicle.generation=3;test(c.Bind(a));test(!c.Current(l.parent));
test(!Pair(531,435));test(Pair(531,610));test(!c.Reset(0));test(c.Reset(2));test(!c.Reset(1));
std::cout<<checks<<" assertions, "<<failures<<" failures\n";return failures?1:0;}
