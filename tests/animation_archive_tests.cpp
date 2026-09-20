#include "animation_archive.hpp"
#include "animation_asset.hpp"
#include "asset_bytes.hpp"
#include <cmath>
#include <iostream>
#include <random>
using namespace forge::animation_detail;
using namespace forge::asset_detail;
using Bytes = std::vector<std::byte>;
void check(bool value, const char* why) {
    if (!value)
        throw std::runtime_error(why);
}
void put(Bytes& bytes, std::size_t at, std::uint32_t value, unsigned count = 4) {
    for (unsigned i = 0; i < count; ++i)
        bytes.at(at + i) = std::byte((value >> (i * 8)) & 255);
}
void reject(const Bytes& bytes, ArchiveKind kind) {
    try {
        validate_archive(bytes, kind);
    } catch (const ArchiveError&) {
        return;
    }
    throw std::runtime_error("Malformed fixture was admitted");
}
int main(int argc, char** argv) {
    try {
        check(argc == 4, "Expected skeleton and clip fixture paths");
        auto s = read_bytes(argv[1], max_archive_bytes), c = read_bytes(argv[2], max_archive_bytes);
        auto skeleton = std::make_shared<Skeleton>(s);
        auto clip = std::make_shared<Clip>(c);
        Sampler sampler(skeleton, clip);
        check(skeleton->info().tracks == 2 && clip->info().duration == 1,
              "Unexpected official fixture");
        check(skeleton->joint_names() == std::vector<std::string>{"Root", "Joint"},
              "Admitted native joint ordering changed");
        const auto rest = skeleton->rest_pose();
        check(rest.size() == 2 && rest[0][13] == 0 && rest[1][13] == 1,
              "Native rest pose disagrees with admitted skeleton hierarchy");
        // Reuse the same sampling context, including backward seeks and discontinuities.
        for (float ratio : {0.f, .5f, 1.f, .1f, .9f, 0.f}) {
            auto pose = sampler.sample(ratio);
            check(pose.size() == 2 && std::abs(pose[1][13] - (1 + ratio)) < .002f,
                  "Numeric pose mismatch");
        }
        auto turn = std::make_shared<Clip>(read_bytes(argv[3], max_archive_bytes));
        Sampler rotating(skeleton, turn);
        for (float ratio : {0.f, .5f, 1.f}) {
            auto pose = rotating.sample(ratio);
            const double angle = ratio * 1.5707963267948966;
            check(std::abs(pose[1][12] + std::sin(angle)) < .002 &&
                      std::abs(pose[1][13] - std::cos(angle)) < .002,
                  "Quaternion/hierarchy sampling mismatch");
        }
        check(std::abs(sampler.sample(.5f)[1][13] - 1.5f) < .002,
              "Independent sampling context was overwritten");
        sampler.reset();
        check(std::abs(sampler.sample(.5f)[1][13] - 1.5f) < .002f, "Sampling reset failed");
        for (auto kind : {ArchiveKind::Skeleton, ArchiveKind::Animation}) {
            const auto& good = kind == ArchiveKind::Skeleton ? s : c;
            for (std::size_t length = 0; length < good.size(); ++length)
                reject(Bytes(good.begin(), good.begin() + length), kind);
            auto bad = good;
            bad.push_back(std::byte{});
            reject(bad, kind);
            bad = good;
            bad[0] = std::byte{0};
            reject(bad, kind);
            bad = good;
            bad[1] = std::byte{'x'};
            reject(bad, kind);
            bad = good;
            put(bad, kind == ArchiveKind::Skeleton ? 14 : 15, 999);
            reject(bad, kind);
        }
        // Minimal reproductions from admission audit: absurd name size and absent NUL.
        auto bad = Bytes(s.begin(), s.begin() + 26);
        put(bad, 18, 1);
        put(bad, 22, 0x7fffffff);
        reject(bad, ArchiveKind::Skeleton);
        bad.resize(27);
        put(bad, 22, 1);
        bad[26] = std::byte{'X'};
        reject(bad, ArchiveKind::Skeleton);
        bad = s;
        put(bad, 18, 1025);
        reject(bad, ArchiveKind::Skeleton);
        bad = s;
        put(bad, 22, 0);
        reject(bad, ArchiveKind::Skeleton);
        // Fixture names are Root\0Joint\0; parent indices follow the names.
        bad = s;
        put(bad, 37, 1, 2);
        reject(bad, ArchiveKind::Skeleton);
        bad = s;
        put(bad, 39, 1, 2);
        reject(bad, ArchiveKind::Skeleton);
        bad = s;
        put(bad, 41, 0x7fc00000);
        reject(bad, ArchiveKind::Skeleton);
        bad = c;
        put(bad, 19, 0);
        reject(bad, ArchiveKind::Animation);
        bad = c;
        put(bad, 19, 0x7f800000);
        reject(bad, ArchiveKind::Animation);
        bad = c;
        put(bad, 23, 0xffffffff);
        reject(bad, ArchiveKind::Animation);
        bad = c;
        put(bad, 27, 256);
        reject(bad, ArchiveKind::Animation);
        bad = c;
        put(bad, 31, 65536);
        reject(bad, ArchiveKind::Animation);
        bad = c;
        put(bad, 35, 0xffffffff);
        reject(bad, ArchiveKind::Animation);
        bad = c;
        put(bad, 47, 1);
        reject(bad, ArchiveKind::Animation);
        // Header 71 + name Lift 4 + two float timepoints 8.
        bad = c;
        bad.at(83) = std::byte{2};
        reject(bad, ArchiveKind::Animation);
        bad = c;
        put(bad, 91, 65535, 2);
        reject(bad, ArchiveKind::Animation);
        bad = c;
        put(bad, 107, 0);
        reject(bad, ArchiveKind::Animation);
        bad = c;
        put(bad, 111, 0x7c00, 2);
        reject(bad, ArchiveKind::Animation);
        check(content_digest({}) ==
                  "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855",
              "SHA256 empty vector");
        std::string abc = "abc";
        check(content_digest(std::as_bytes(std::span(abc))) ==
                  "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad",
              "SHA256 abc vector");
        std::string long_vector = "abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq";
        check(content_digest(std::as_bytes(std::span(long_vector))) ==
                  "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1",
              "SHA256 padding/multiblock vector");
        // Bounded deterministic mutations. Rejected bytes never reach Ozz.
        // Accepted supported variants must still load/sample safely through the same boundary.
        std::mt19937 random(0x6d);
        unsigned rejected = 0, admitted = 0;
        for (unsigned i = 0; i < 4000; ++i) {
            bool skel = i % 2 == 0;
            auto bytes = skel ? s : c;
            bytes[random() % bytes.size()] ^= std::byte(1u << (random() % 8));
            auto kind = skel ? ArchiveKind::Skeleton : ArchiveKind::Animation;
            try {
                validate_archive(bytes, kind);
            } catch (const ArchiveError&) {
                ++rejected;
                continue;
            }
            // Numeric-domain rejection remains possible after structurally safe evaluation.
            try {
                auto ms = skel ? std::make_shared<Skeleton>(bytes) : skeleton;
                auto mc = skel ? clip : std::make_shared<Clip>(bytes);
                Sampler candidate(ms, mc);
                for (float t : {0.f, .25f, .5f, 1.f, 0.f})
                    candidate.sample(t);
                ++admitted;
            } catch (const ArchiveError&) {
                ++rejected;
            }
        }
        check(rejected > 1000 && admitted > 0, "Mutation coverage did not exercise both outcomes");
        std::cout << "Archive admission passed; bounded mutations rejected=" << rejected
                  << " admitted=" << admitted << "\n";
    } catch (const std::exception& e) {
        std::cerr << e.what() << "\n";
        return 1;
    }
}
