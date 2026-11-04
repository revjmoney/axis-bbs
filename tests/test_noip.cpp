#include "bbs/noip.h"

#include "doctest.h"

using axis::bbs::NoIpReply;
using axis::bbs::noipReply;

TEST_CASE("No-IP good and nochg replies are success and carry the IP") {
    auto r = noipReply("good 203.0.113.7\r\n");
    CHECK(r.ok);
    CHECK_FALSE(r.stop);
    CHECK(r.text == "good 203.0.113.7");
    r = noipReply("nochg 203.0.113.7");
    CHECK(r.ok);
    CHECK_FALSE(r.stop);
}

TEST_CASE("No-IP refusals stop the updater until the settings change") {
    for (const char* s : {"nohost", "badauth", "badagent", "!donator", "abuse"}) {
        auto r = noipReply(s);
        CHECK_FALSE(r.ok);
        CHECK(r.stop);
    }
}

TEST_CASE("a No-IP 911 or an unknown reply is a failure that retries later") {
    auto r = noipReply("911");
    CHECK_FALSE(r.ok);
    CHECK_FALSE(r.stop);
    r = noipReply("");
    CHECK_FALSE(r.ok);
    CHECK_FALSE(r.stop);
}

TEST_CASE("a DDNS key with several hosts answers one line per host; any refusal stops") {
    auto r = noipReply("good 203.0.113.7\nnochg 203.0.113.7\n");
    CHECK(r.ok);
    r = noipReply("good 203.0.113.7\nnohost\n");
    CHECK_FALSE(r.ok);
    CHECK(r.stop);
}

TEST_CASE("the No-IP update URL names the host and IP") {
    CHECK(axis::bbs::noipUrl("all.ddnskey.com", "203.0.113.7") ==
          "https://dynupdate.no-ip.com/nic/update?hostname=all.ddnskey.com&myip=203.0.113.7");
}
