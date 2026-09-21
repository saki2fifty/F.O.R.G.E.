#pragma once
#include "texture_asset_preview.hpp"
#include "texture_bundle_validation.hpp"
#include "texture_gpu.hpp"
#include "texture_preview.hpp"
#include <forge/asset_build.hpp>
void check_texture_preview(forge::DiligentPresentation& presentation,
                           Diligent::IDeviceContext* context, const std::filesystem::path& images) {
    using namespace forge;
    TexturePreviewRenderer preview(presentation);
    TexturePreviewSettings settings;
    settings.display = TexturePreviewDisplay::Data;
    settings.nearest = true;
    settings.checker = false;
    for (const auto dimension :
         {TextureDimension::D2, TextureDimension::D2Array, TextureDimension::Cube,
          TextureDimension::CubeArray, TextureDimension::D3}) {
        TextureData data;
        data.dimension = dimension;
        data.semantic = TextureSemantic::Data;
        data.width = data.height = 8;
        data.mips = 2;
        data.depth = dimension == TextureDimension::D3 ? 4 : 1;
        data.layers =
            dimension == TextureDimension::D2Array || dimension == TextureDimension::CubeArray ? 2
                                                                                               : 1;
        const unsigned faces =
            dimension == TextureDimension::Cube || dimension == TextureDimension::CubeArray ? 6 : 1;
        for (unsigned layer = 0; layer < data.layers; ++layer)
            for (unsigned face = 0; face < faces; ++face)
                for (unsigned mip = 0; mip < data.mips; ++mip) {
                    const auto layout = texture_layout(data, mip);
                    auto& bytes = data.subresources.emplace_back(layout.bytes);
                    for (unsigned z = 0; z < layout.depth; ++z)
                        for (std::size_t pixel = 0; pixel < layout.slice_bytes; pixel += 4) {
                            const auto at = z * layout.slice_bytes + pixel;
                            bytes[at] = std::byte(20 + layer * 100 + face * 10);
                            bytes[at + 1] = std::byte(30 + mip * 100);
                            bytes[at + 2] = std::byte(40 + z * 50);
                            bytes[at + 3] = std::byte{255};
                        }
                }
        auto native = upload_texture(presentation.device(), data);
        for (unsigned layer = 0; layer < data.layers; ++layer)
            for (unsigned face = 0; face < faces; ++face)
                for (unsigned mip = 0; mip < data.mips; ++mip)
                    for (unsigned z = 0; z < std::max(1u, data.depth >> mip); ++z) {
                        settings.layer = layer;
                        settings.face = face;
                        settings.mip = mip;
                        settings.depth = z;
                        const auto pixels =
                            readback(presentation.device(), context,
                                     preview.render(context, native, settings, 32, 32));
                        const std::array<unsigned char, 4> expected{
                            static_cast<unsigned char>(20 + layer * 100 + face * 10),
                            static_cast<unsigned char>(30 + mip * 100),
                            static_cast<unsigned char>(40 + z * 50), 255};
                        require(std::all_of(pixels.begin(), pixels.end(),
                                            [&](const auto& p) { return p == expected; }),
                                "Texture preview read the wrong mip/layer/cube face/volume slice");
                    }
    }
    settings = {};
    settings.nearest = true;
    settings.checker = false;
    TextureData data;
    data.width = data.height = 1;
    data.format = TextureFormat::RGBA8Srgb;
    data.subresources = {{std::byte{128}, std::byte{64}, std::byte{32}, std::byte{128}}};
    auto native = upload_texture(presentation.device(), data);
    auto pixels =
        readback(presentation.device(), context, preview.render(context, native, settings, 96, 96));
    require(std::abs(int(pixels[0][0]) - 128) <= 1 && std::abs(int(pixels[0][1]) - 64) <= 1 &&
                std::abs(int(pixels[0][2]) - 32) <= 1,
            "Texture color preview applied missing or duplicate sRGB transfer");
    settings.channel = TexturePreviewChannel::Alpha;
    pixels =
        readback(presentation.device(), context, preview.render(context, native, settings, 96, 96));
    require(pixels[0] == std::array<unsigned char, 4>{128, 128, 128, 255},
            "Alpha preview applied gamma to alpha");
    settings.channel = TexturePreviewChannel::Red;
    pixels =
        readback(presentation.device(), context, preview.render(context, native, settings, 96, 96));
    require(std::abs(int(pixels[0][0]) - 55) <= 1 && pixels[0][0] == pixels[0][1],
            "Isolated channel did not show the sampled linear value");
    settings.channel = TexturePreviewChannel::Rgba;
    settings.checker = true;
    pixels =
        readback(presentation.device(), context, preview.render(context, native, settings, 96, 96));
    save(pixels, 96, 96, images / "texture-preview-alpha-checker.ppm");
    require(pixels[0] != pixels[12] && pixels[0][3] == 255,
            "Transparent preview lost its checkerboard");
    data.format = TextureFormat::RGBA8;
    data.semantic = TextureSemantic::Data;
    data.subresources = {{std::byte{128}, std::byte{0}, std::byte{0}, std::byte{128}}};
    native = upload_texture(presentation.device(), data);
    settings.display = TexturePreviewDisplay::Data;
    settings.checker = false;
    settings.alpha = TextureAlpha::Premultiplied;
    pixels =
        readback(presentation.device(), context, preview.render(context, native, settings, 96, 96));
    require(pixels[0][0] == 255, "Premultiplied texture preview was treated as straight alpha");
    data.width = 2;
    data.subresources = {{std::byte{255}, std::byte{0}, std::byte{0}, std::byte{255}, std::byte{0},
                          std::byte{255}, std::byte{0}, std::byte{255}}};
    native = upload_texture(presentation.device(), data);
    settings.region = {.5f, 0, 1, 1};
    pixels =
        readback(presentation.device(), context, preview.render(context, native, settings, 96, 96));
    require(std::all_of(pixels.begin(), pixels.end(),
                        [](const auto& p) { return p[0] == 0 && p[1] == 255; }),
            "Zoomed texture preview crop sampled texels outside the visible region");
    settings.region = {0, 0, 1, 1};
    settings.nearest = false;
    pixels =
        readback(presentation.device(), context, preview.render(context, native, settings, 96, 96));
    require(pixels[48][0] > 60 && pixels[48][1] > 60, "Linear preview used point filtering");
    save(pixels, 96, 96, images / "texture-preview-linear.ppm");
    auto* previous = preview.output();
    const auto rejected = [&](TexturePreviewSettings invalid) {
        bool failed = false;
        try {
            preview.render(context, native, invalid, 96, 96);
        } catch (const std::exception&) {
            failed = true;
        }
        require(failed && preview.output() == previous,
                "Invalid preview changed the previous usable image");
    };
    auto invalid = settings;
    invalid.mip = 10;
    rejected(invalid);
    invalid = settings;
    invalid.layer = 1;
    rejected(invalid);
    invalid = settings;
    invalid.face = 6;
    rejected(invalid);
    invalid = settings;
    invalid.depth = 1;
    rejected(invalid);
    invalid = settings;
    invalid.exposure = std::numeric_limits<float>::quiet_NaN();
    rejected(invalid);
    invalid = settings;
    invalid.region = {.5f, 0, .5f, 1};
    rejected(invalid);
    require(readback(presentation.device(), context, preview.output()) == pixels,
            "Rejected texture preview changed previous image pixels");
    {
        TextureData hdr;
        hdr.width = hdr.height = 1;
        hdr.format = TextureFormat::RGBA32Float;
        hdr.semantic = TextureSemantic::HdrColor;
        hdr.subresources.emplace_back(16);
        const std::array<float, 4> value{8, 8, 8, 1};
        std::memcpy(hdr.subresources[0].data(), value.data(), 16);
        auto texture = upload_texture(presentation.device(), hdr);
        TexturePreviewSettings display;
        display.display = TexturePreviewDisplay::Hdr;
        display.checker = false;
        const auto high = readback(presentation.device(), context,
                                   preview.render(context, texture, display, 96, 96));
        display.exposure = -3;
        const auto low = readback(presentation.device(), context,
                                  preview.render(context, texture, display, 96, 96));
        require(high[0][0] > low[0][0] && low[0][0] > 230 && high[0][0] < 255,
                "HDR preview clipped radiance before exposure/tone mapping");
        save(high, 96, 96, images / "texture-preview-hdr.ppm");
    }
    {
        const auto root = images / ("texture-preview-project-" + AssetId::generate().str());
        std::filesystem::create_directories(root / "Assets");
        struct Cleanup {
            std::filesystem::path path;
            ~Cleanup() {
                std::error_code error;
                std::filesystem::remove_all(path, error);
            }
        } cleanup{root};
        const auto id = AssetId::generate();
        const auto publish = [&](unsigned char red, std::uint64_t generation) {
            TextureData texture;
            texture.width = texture.height = 1;
            texture.format = TextureFormat::RGBA8Srgb;
            texture.subresources = {{std::byte{red}, std::byte{0}, std::byte{0}, std::byte{255}}};
            const auto bytes = encode_texture(texture);
            TextureBundleIndex index;
            index.variants = {{texture.semantic, texture_variant_file(texture.semantic),
                               asset_detail::content_digest(bytes), bytes.size()}};
            AssetBuildInput input;
            input.source_digest = asset_detail::content_digest(bytes);
            input.importer = "forge.texture.preview.fixture";
            input.importer_revision = std::string(64, 'a');
            input.output_format = "forge.texture-bundle";
            input.platform = "windows";
            input.backend = "d3d12";
            input.profile = "desktop";
            DerivedDataCache cache(root);
            const auto artifact = cache.publish(
                input,
                {{"texture.json", encode_texture_bundle_index(index)},
                 {index.variants[0].file, bytes}},
                [](const auto& a) { (void)asset_detail::validate_texture_bundle(a.files); });
            auto catalog = std::make_shared<AssetCatalog>(root);
            catalog->add(
                {id,
                 "texture",
                 "Assets/image.png",
                 1,
                 {},
                 {{"forge.import",
                   {{"version", 1},
                    {"generation", generation},
                    {"key", input.key()},
                    {"source_digest", input.source_digest},
                    {"importer", input.importer},
                    {"importer_revision", input.importer_revision},
                    {"output_format", input.output_format},
                    {"output_version", 1},
                    {"artifact_digest", asset_build_digest(artifact.manifest.at("files"))}}}}});
            return catalog;
        };
        TextureAssetPreview owner(presentation, context, root);
        auto catalog = publish(50, 1);
        owner.select(catalog, {id});
        const auto finish = [&] {
            const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
            while (owner.pending() && std::chrono::steady_clock::now() < deadline) {
                owner.pump();
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
            require(!owner.pending(), "Texture preview resource worker stalled");
            owner.pump();
        };
        finish();
        require(owner.data() && owner.error().empty(),
                "Texture preview failed a valid selected asset");
        const auto first = readback(presentation.device(), context, owner.render(32, 32));
        const auto rendered = owner.render_count();
        require(std::abs(int(first[0][0]) - 50) <= 1,
                "Texture preview did not display the cooked selection");
        require(readback(presentation.device(), context, owner.render(32, 32)) == first &&
                    owner.render_count() == rendered,
                "Cached texture preview changed without revision/settings changes");
        catalog = publish(200, 2);
        owner.select(catalog, {id});
        finish();
        const auto replacement = readback(presentation.device(), context, owner.render(32, 32));
        require(replacement[0][0] > first[0][0] + 100,
                "Published revision did not refresh texture preview");
        auto invalid_catalog = std::make_shared<AssetCatalog>(*catalog);
        auto record = invalid_catalog->records().at(id);
        record.metadata["forge.import"]["key"] = std::string(64, 'b');
        record.metadata["forge.import"]["generation"] = std::uint64_t(3);
        invalid_catalog->replace(record);
        owner.select(invalid_catalog, {id});
        finish();
        require(!owner.error().empty() && owner.data() &&
                    readback(presentation.device(), context, owner.render(32, 32)) == replacement,
                "Failed texture preview replacement discarded the last-good revision");
        owner.select(catalog, {id}, TextureSemantic::Normal);
        finish();
        require(!owner.error().empty() && !owner.data() && owner.render(32, 32) == nullptr,
                "Missing semantic variant silently displayed the previous color interpretation");
    }
}
