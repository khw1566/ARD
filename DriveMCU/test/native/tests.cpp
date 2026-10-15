#include <cassert>
#include <cmath>
#include <cstdio>
#include <string>
#include <initializer_list>
#include "BoundedLine.h"
#include "BoundedAht20.h"
#include "CommandValidation.h"
uint32_t testMillis = 100;
FakeWire Wire;
int main() {
    for (const char *command : {"b200,-200", "mzr-652", "df", "dl0", "mzs43.48", "mzm0", "home", "x", "md100", "mxs1000", "bv64"})
        assert(validDriveCommand(command));
    for (const char *command : {"b1,2junk", "b1,2,3", "b1e2,20", "mxr1e2", "mxr", "mxr1.5", "mxrnan", "mxr99999999999", "mxs0", "mya-2", "mzsinf", "mzh0", "md-1", "gps2", "mzm2", "mzz200"})
        assert(!validDriveCommand(command));
    BoundedLine line;
    for (char c : {'m','x','r','2','0','0'}) assert(line.push(c)==BoundedLine::Pending);
    assert(line.push('\n')==BoundedLine::Ready);
    assert(std::string(line.value())=="mxr200");
    line.reset(); assert(line.push('\r')==BoundedLine::Pending);
    for(int i=0;i<128;++i) line.push('b');
    line.push(8); line.push('x');
    assert(line.push('\n')==BoundedLine::TooLong);
    assert(!line.partial());
    line.push('x'); assert(line.push('\n')==BoundedLine::Ready);
    line.reset();
    for(int i=0;i<127;++i) line.push('a');
    assert(line.push('\n')==BoundedLine::Ready);
    line.reset(); line.push('x');line.push(8); assert(!line.partial());
    line.push('x');line.push('\0');assert(line.push('\n')==BoundedLine::TooLong);

    BoundedAht20 ok;
    assert(ok.begin());
    float t=0,h=0;
    const uint32_t before=testMillis;
    assert(!ok.poll(t,h)); assert(testMillis==before);
    testMillis+=80; assert(ok.poll(t,h));
    assert(std::fabs(t-50)<.01 && std::fabs(h-50)<.01);
    assert(ok.healthy()); testMillis+=10001; assert(!ok.healthy());

    Wire=FakeWire(); Wire.missing=true;
    BoundedAht20 missing; assert(!missing.begin());
    Wire=FakeWire(); Wire.busy=true;
    BoundedAht20 stuck;
    const uint32_t start=testMillis;
    assert(!stuck.begin()); assert(testMillis-start<=250);
    Wire=FakeWire(); BoundedAht20 measuring;
    assert(measuring.begin()); assert(!measuring.poll(t,h));
    Wire.busy=true; testMillis+=80;
    const uint32_t busyTime=testMillis;
    assert(!measuring.poll(t,h)); assert(testMillis==busyTime);
    testMillis+=180; assert(!measuring.poll(t,h));
    assert(!measuring.healthy());
    const unsigned count=Wire.writes;
    testMillis+=10; assert(!measuring.poll(t,h)); assert(Wire.writes==count);
    testMillis+=5000; Wire.busy=false; assert(!measuring.poll(t,h));
    testMillis+=80; assert(measuring.poll(t,h));
    Wire.missing=true; testMillis+=5000; assert(!measuring.poll(t,h));
    // Unsigned elapsed time remains valid across the millis() rollover.
    Wire=FakeWire(); testMillis=UINT32_MAX-40; BoundedAht20 wrap;
    assert(wrap.begin()); assert(!wrap.poll(t,h));testMillis+=80;assert(wrap.poll(t,h));
    std::puts("Drive native tests passed: bounded commands, missing/busy sensors, stale data, clock rollover");
}
