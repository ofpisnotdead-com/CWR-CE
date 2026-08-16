#pragma once

#include <Poseidon/Graphics/Textures/TextureBank.hpp>
#include <Poseidon/Graphics/Dummy/TextureDummy.hpp>
#include <Poseidon/Foundation/Containers/Array.hpp>

namespace Poseidon
{
class TextBankDummy : public AbstractTextBank
{
  private:
    LLinkArray<TextureDummy> _texture;

  public:
    TextBankDummy();
    ~TextBankDummy() override;

    int Find(RStringB name1, TextureDummy* interpolate = nullptr);
    Ref<Texture> Load(RStringB) override;
    Ref<Texture> LoadInterpolated(RStringB, RStringB, float) override { return nullptr; }
    MipInfo UseMipmap(Texture* tex, int level, int top) override { return MipInfo(tex, level); }

    void Compact() override { _texture.Compact(); }
    void ReleaseMipmap() {}
    void Preload() override {}

    int NTextures() const override { return _texture.Size(); }
    Texture* GetTexture(int i) const override { return _texture[i]; }

    void FlushTextures() override { Compact(); }
    // Drop the cached texture links so a kept-alive engine doesn't accumulate stale
    // (auto-nulled) weak-link entries across in-process re-mounts.
    void ReleaseAllTextures() override { _texture.Clear(); }
    void FlushBank(QFBank* bank) override {}
};

} // namespace Poseidon
