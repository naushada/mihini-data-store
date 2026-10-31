#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

#include "lwm2m/coap.hpp"
#include "lwm2m/tlv.hpp"
#include "lwm2m/senml.hpp"
#include "lwm2m/model.hpp"
#include "lwm2m/engine.hpp"

using namespace lwm2m;

// Build a CoAP request whose Uri-Path options spell out `path` (e.g. "/3/0/13").
static coap::Message get(const std::string& path) {
    coap::Message m;
    m.type = coap::Type::Con;
    m.code = coap::Code::Get;
    m.id = 0x1234;
    m.token = "ab";
    std::size_t pos = 1;
    while (pos <= path.size()) {
        std::size_t slash = path.find('/', pos);
        if (slash == std::string::npos) slash = path.size();
        m.options.push_back({coap::OPT_URI_PATH, path.substr(pos, slash - pos)});
        pos = slash + 1;
    }
    return m;
}

TEST(Coap, Roundtrip) {
    coap::Message m;
    m.type = coap::Type::Con;
    m.code = coap::Code::Get;
    m.id = 0x0BAD;
    m.token = std::string("\x01\x02", 2);
    m.options.push_back({coap::OPT_URI_PATH, "3"});
    m.options.push_back({coap::OPT_URI_PATH, "0"});
    m.options.push_back({coap::OPT_URI_PATH, "13"});
    m.options.push_back({coap::OPT_ACCEPT, std::string(1, '\x00')});
    m.payload = "hello";

    const std::string wire = coap::encode(m);
    EXPECT_FALSE(wire.empty());

    coap::Message d;
    EXPECT_TRUE(coap::decode(wire, d));
    EXPECT_EQ(d.type, coap::Type::Con);
    EXPECT_EQ(d.code, coap::Code::Get);
    EXPECT_EQ(d.id, 0x0BAD);
    EXPECT_EQ(d.token, m.token);
    EXPECT_EQ(d.uri_path(), "/3/0/13");
    EXPECT_EQ(d.payload, "hello");
    ASSERT_NE(d.find_option(coap::OPT_ACCEPT), nullptr);
    EXPECT_EQ(*d.find_option(coap::OPT_ACCEPT), std::string(1, '\x00'));

    // Code class/detail helpers.
    EXPECT_EQ(coap::code_class(coap::Code::Content), 2);
    EXPECT_EQ(coap::code_detail(coap::Code::Content), 5);
}

TEST(Tlv, Roundtrip) {
    std::vector<tlv::Record> recs;
    recs.push_back({tlv::TYPE_RESOURCE, 0, "Acme"});
    recs.push_back({tlv::TYPE_RESOURCE, 13, "42"});

    const std::string wire = tlv::encode_instance(0, recs);
    std::vector<tlv::Record> decoded;
    EXPECT_TRUE(tlv::decode(wire, decoded));
    const auto leafs = tlv::resources(decoded);
    ASSERT_EQ(leafs.size(), 2u);
    EXPECT_TRUE(leafs[0].id == 0 && leafs[0].value == "Acme");
    EXPECT_TRUE(leafs[1].id == 13 && leafs[1].value == "42");

    // Single-resource record.
    const std::string single = tlv::encode_resource(13, "hi");
    std::vector<tlv::Record> sr;
    EXPECT_TRUE(tlv::decode(single, sr));
    ASSERT_EQ(sr.size(), 1u);
    EXPECT_EQ(sr[0].type, tlv::TYPE_RESOURCE);
    EXPECT_EQ(sr[0].id, 13);
    EXPECT_EQ(sr[0].value, "hi");
}

TEST(Senml, Roundtrip) {
    std::vector<senml::Record> recs;

    senml::Record r;
    r.baseName = "/3/0/";
    r.name = "13";
    r.kind = senml::Kind::Numeric;
    r.numericValue = 42;
    r.isFloat = false;
    recs.push_back(r);

    senml::Record r2;
    r2.baseName = "/3/0/";
    r2.name = "0";
    r2.kind = senml::Kind::String;
    r2.stringValue = "open";
    recs.push_back(r2);

    const std::string json = senml::encode_json(recs);
    EXPECT_FALSE(json.empty());

    std::vector<senml::Record> back;
    EXPECT_TRUE(senml::decode_json(json, back));
    ASSERT_EQ(back.size(), 2u);
    EXPECT_EQ(back[0].path(), "/3/0/13");
    EXPECT_EQ(back[0].kind, senml::Kind::Numeric);
    EXPECT_EQ(back[0].numericValue, 42);
    EXPECT_EQ(back[1].kind, senml::Kind::String);
    EXPECT_EQ(back[1].stringValue, "open");

    // Mismatched base names must be rejected.
    recs[1].baseName = "/4/0/";
    EXPECT_TRUE(senml::encode_json(recs).empty());
}

TEST(Engine, Read) {
    Engine e;

    ObjectDescriptor od;
    od.oid = 3;
    od.name = "Device";
    ObjectInstance inst;
    inst.iid = 0;

    Resource r13;
    r13.rid = 13;
    r13.type = ResourceType::Integer;
    r13.ops = Operations::R;
    r13.read = []() { return "42"; };
    inst.resources[13] = r13;

    Resource r0;
    r0.rid = 0;
    r0.type = ResourceType::String;
    r0.ops = Operations::R;
    r0.read = []() { return "Acme"; };
    inst.resources[0] = r0;

    od.instances[0] = std::move(inst);
    e.store().add_object(std::move(od));

    // Resource read -> plain text.
    auto resp = e.handle(get("/3/0/13"));
    ASSERT_EQ(resp.size(), 1u);
    coap::Message d;
    EXPECT_TRUE(coap::decode(resp[0], d));
    EXPECT_EQ(d.code, coap::Code::Content);
    EXPECT_EQ(d.payload, "42");

    // Object read -> TLV.
    auto resp2 = e.handle(get("/3"));
    ASSERT_EQ(resp2.size(), 1u);
    coap::Message d2;
    EXPECT_TRUE(coap::decode(resp2[0], d2));
    EXPECT_EQ(d2.code, coap::Code::Content);
    EXPECT_NE(d2.find_option(coap::OPT_CONTENT_FORMAT), nullptr);

    // Missing resource -> 4.04.
    auto resp3 = e.handle(get("/3/0/99"));
    coap::Message d3;
    EXPECT_TRUE(coap::decode(resp3[0], d3));
    EXPECT_EQ(d3.code, coap::Code::NotFound);

    // Unknown object -> 4.04.
    auto resp4 = e.handle(get("/99"));
    coap::Message d4;
    EXPECT_TRUE(coap::decode(resp4[0], d4));
    EXPECT_EQ(d4.code, coap::Code::NotFound);
}

TEST(Engine, WriteNotify) {
    Engine e;

    std::string value = "off";

    ObjectDescriptor od;
    od.oid = 3;
    od.name = "Device";
    ObjectInstance inst;
    inst.iid = 0;

    Resource r;
    r.rid = 13;
    r.type = ResourceType::Integer;
    r.ops = Operations::RW;
    r.observable = true;
    r.read = [&]() { return value; };
    r.write = [&](const std::string& v) { value = v; return 0; };
    inst.resources[13] = r;

    od.instances[0] = std::move(inst);
    e.store().add_object(std::move(od));

    // Observe register.
    coap::Message reg = get("/3/0/13");
    reg.token = "tok1";
    reg.options.insert(reg.options.begin(), {coap::OPT_OBSERVE, std::string(1, '\0')});
    auto resp = e.handle(reg);
    ASSERT_EQ(resp.size(), 1u);
    coap::Message d;
    EXPECT_TRUE(coap::decode(resp[0], d));
    EXPECT_EQ(d.code, coap::Code::Content);
    EXPECT_NE(d.find_option(coap::OPT_OBSERVE), nullptr);
    EXPECT_EQ(d.payload, "off");

    // Write -> 2.04 Changed plus a Notify frame to the observer.
    coap::Message w;
    w.type = coap::Type::Con;
    w.code = coap::Code::Put;
    w.id = 0x2222;
    w.token = "x";
    w.options.push_back({coap::OPT_URI_PATH, "3"});
    w.options.push_back({coap::OPT_URI_PATH, "0"});
    w.options.push_back({coap::OPT_URI_PATH, "13"});
    w.payload = "on";

    auto resp2 = e.handle(w);
    ASSERT_GE(resp2.size(), 2u);

    coap::Message d2;
    EXPECT_TRUE(coap::decode(resp2[0], d2));
    EXPECT_EQ(d2.code, coap::Code::Changed);
    EXPECT_EQ(value, "on");

    coap::Message n;
    EXPECT_TRUE(coap::decode(resp2[1], n));
    EXPECT_EQ(n.code, coap::Code::Content);
    EXPECT_EQ(n.token, "tok1");
    EXPECT_EQ(n.payload, "on");
    EXPECT_NE(n.find_option(coap::OPT_OBSERVE), nullptr);
}

TEST(Engine, Execute) {
    Engine e;
    int calls = 0;

    ObjectDescriptor od;
    od.oid = 3;
    od.name = "Device";
    ObjectInstance inst;
    inst.iid = 0;

    Resource r;
    r.rid = 4;
    r.type = ResourceType::None;
    r.ops = Operations::E;
    r.execute = [&](const std::string&) { ++calls; return 0; };
    inst.resources[4] = r;

    od.instances[0] = std::move(inst);
    e.store().add_object(std::move(od));

    coap::Message req;
    req.type = coap::Type::Con;
    req.code = coap::Code::Post;
    req.id = 0x3333;
    req.token = "e";
    req.options.push_back({coap::OPT_URI_PATH, "3"});
    req.options.push_back({coap::OPT_URI_PATH, "0"});
    req.options.push_back({coap::OPT_URI_PATH, "4"});

    auto resp = e.handle(req);
    ASSERT_FALSE(resp.empty());
    coap::Message d;
    EXPECT_TRUE(coap::decode(resp[0], d));
    EXPECT_EQ(d.code, coap::Code::Changed);
    EXPECT_EQ(calls, 1);
}

TEST(Engine, Send) {
    std::vector<senml::Record> recs;
    senml::Record r;
    r.baseName = "/3/0/";
    r.name = "13";
    r.kind = senml::Kind::Numeric;
    r.numericValue = 7;
    r.isFloat = false;
    recs.push_back(r);

    const std::string wire = Engine::build_send(0x0102, "t", recs);
    coap::Message d;
    EXPECT_TRUE(coap::decode(wire, d));
    EXPECT_EQ(d.code, coap::Code::Post);
    EXPECT_EQ(d.uri_path(), "/dp");
    EXPECT_NE(d.find_option(coap::OPT_CONTENT_FORMAT), nullptr);

    std::vector<senml::Record> back;
    EXPECT_TRUE(senml::decode_json(d.payload, back));
    ASSERT_EQ(back.size(), 1u);
    EXPECT_EQ(back[0].path(), "/3/0/13");
    EXPECT_EQ(back[0].numericValue, 7);
}
