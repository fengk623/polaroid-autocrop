// RFC-0013 metadata / XMP tests.
#include <gtest/gtest.h>

#include "fk/metadata.hpp"

using namespace fk;

TEST(Xmp, BuildAndParseIds) {
    XmpIds ids{"proj-1", "scan-2", "item-3", "ev-4", "inst-5", 7, "P03", "image_area"};
    const std::string x = buildXmp(ids);
    const XmpIds back = parseXmpIds(x);
    EXPECT_EQ(back.projectId, "proj-1");
    EXPECT_EQ(back.scanId, "scan-2");
    EXPECT_EQ(back.itemId, "item-3");
    EXPECT_EQ(back.editVersionId, "ev-4");
    EXPECT_EQ(back.version, 7);
    EXPECT_EQ(back.label, "P03");
    EXPECT_EQ(back.frameMode, "image_area");
    EXPECT_NE(x.find("xmpMM:DocumentID=\"uuid:ev-4\""), std::string::npos);
}

TEST(Xmp, ParsesElementFormAndDocumentIdFallback) {
    const std::string x =
        "<rdf:Description><fk:ItemID>abc</fk:ItemID><xmpMM:DocumentID>uuid:dead-beef</xmpMM:DocumentID>"
        "</rdf:Description>";
    const XmpIds ids = parseXmpIds(x);
    EXPECT_EQ(ids.itemId, "abc");
    EXPECT_EQ(ids.editVersionId, "dead-beef");
}

TEST(Xmp, CarriesDcFieldsOnly) {
    MetadataSet src;
    src.sourceXmp = "<dc:title><rdf:Alt><rdf:li>Beach</rdf:li></rdf:Alt></dc:title><exif:GPSLatitude>1</"
                    "exif:GPSLatitude>";
    const std::string x = buildXmp(XmpIds{}, &src);
    EXPECT_NE(x.find("Beach"), std::string::npos);
    EXPECT_EQ(x.find("GPSLatitude"), std::string::npos);
}

TEST(Privacy, PresetsRemoveTheDocumentedFields) {
    MetadataSet m;
    m.artist = "Ann";
    m.hostComputer = "ann-mac";
    m.make = "EPSON";
    m.hasGps = true;
    m.fields = {{"Make", "EPSON", PrivacyClass::safe},
                {"Artist", "Ann", PrivacyClass::identity},
                {"GPSInfo", "present", PrivacyClass::location}};
    const auto keep = applyPrivacy(m, PrivacyPreset::keep_all_safe);
    EXPECT_EQ(keep.artist, "Ann");
    EXPECT_FALSE(keep.hasGps);
    const auto loc = applyPrivacy(m, PrivacyPreset::remove_location);
    EXPECT_EQ(loc.fields.size(), 2u);
    const auto id = applyPrivacy(m, PrivacyPreset::remove_identity);
    EXPECT_TRUE(id.artist.empty());
    EXPECT_TRUE(id.hostComputer.empty());
    EXPECT_EQ(id.make, "EPSON");
    EXPECT_EQ(id.fields.size(), 1u);
}

TEST(Privacy, RemoveIdentityDropsCreatorFromSourceXmp) {
    // formerly KnownIssue.FK004_RemoveIdentityDropsCreatorFromSourceXmp
    MetadataSet m;
    m.artist = "Ann Example";
    m.sourceXmp = "<dc:creator><rdf:Seq><rdf:li>Ann Example</rdf:li></rdf:Seq></dc:creator>"
                  "<dc:rights><rdf:Alt><rdf:li>CC BY</rdf:li></rdf:Alt></dc:rights>";
    m.fields = {{"Artist", "Ann Example", PrivacyClass::identity}};
    const MetadataSet filtered = applyPrivacy(m, PrivacyPreset::remove_identity);
    const std::string xmp = buildXmp(XmpIds{}, &filtered);
    EXPECT_EQ(xmp.find("Ann Example"), std::string::npos) << xmp;
    EXPECT_NE(xmp.find("CC BY"), std::string::npos) << "rights are kept";
    // kept by the other presets
    EXPECT_NE(buildXmp(XmpIds{}, &m).find("Ann Example"), std::string::npos);
    const MetadataSet keep = applyPrivacy(m, PrivacyPreset::keep_all_safe);
    EXPECT_NE(buildXmp(XmpIds{}, &keep).find("Ann Example"), std::string::npos);
}

TEST(Privacy, WrittenFieldsAreStatedPerChannel) {
    MetadataSet m;
    m.make = "EPSON";
    m.artist = "Ann";
    m.hostComputer = "host";
    m.sourceXmp = "<dc:creator><rdf:Seq><rdf:li>Ann</rdf:li></rdf:Seq></dc:creator>"
                  "<dc:title><rdf:Alt><rdf:li>T</rdf:li></rdf:Alt></dc:title>";
    auto names = [](const std::vector<MetadataField>& v) {
        std::vector<std::string> n;
        for (const auto& f : v)
            n.push_back(f.name);
        return n;
    };
    const auto keepTiff = writtenMetadata(applyPrivacy(m, PrivacyPreset::keep_all_safe), TagChannel::tiff);
    EXPECT_EQ(names(keepTiff), (std::vector<std::string>{"TIFF Make", "TIFF Artist", "TIFF HostComputer",
                                                         "XMP dc:title", "XMP dc:creator"}));
    const auto idPng = writtenMetadata(applyPrivacy(m, PrivacyPreset::remove_identity), TagChannel::none);
    EXPECT_EQ(names(idPng), (std::vector<std::string>{"XMP dc:title"}));
    EXPECT_EQ(idPng.front().value, "T");
}
