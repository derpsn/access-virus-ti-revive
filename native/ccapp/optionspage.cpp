// optionspage.cpp - see optionspage.h.
#include "optionspage.h"
#include "cc/diag.h"
#include "vstgui/lib/controls/cbuttons.h"
#include "vstgui/lib/controls/ctextlabel.h"
#include "vstgui/lib/controls/icontrollistener.h"
#include <string>
#include <vector>

using namespace VSTGUI;

namespace tirevive {
namespace {

const CColor kBack(43, 43, 43), kText(230, 230, 230), kDim(160, 160, 160), kAccent(240, 170, 60);
constexpr CCoord W = kOptionsWidth, H = kOptionsHeight, M = 20, kRow = 58;
enum Tag { kBack_ = 1, kFirstOption = 100 };

class OptionsPage : public CViewContainer, public IControlListener {
public:
    explicit OptionsPage(std::function<void()> back) : CViewContainer(CRect(0, 0, W, H)), back_(std::move(back))
    {
        setBackgroundColor(kBack);
        build();
    }

    void activate(bool on)
    {
        setVisible(on);
        if (on) {
            message_->setText("");
            refresh();
        }
    }

    void valueChanged(CControl *c) override
    {
        if (c->getTag() == kBack_) {
            if (c->getValue() >= 0.5f)
                back_();
            return;
        }
        const auto &opts = vti::cc::plugin_options();
        const int i = c->getTag() - kFirstOption;
        if (i < 0 || i >= (int)opts.size())
            return;
        const bool on = c->getValue() >= 0.5f;
        std::string err;
        if (!vti::cc::set_plugin_option(opts[(size_t)i].name, on, err))
            message_->setText(("Not changed: " + err).c_str());
        else
            message_->setText((std::string(opts[(size_t)i].title) + (on ? ": on" : ": off") +
                               " - from the next start of the plug-in (reload it or restart the DAW).")
                                  .c_str());
        refresh();
    }

private:
    std::function<void()> back_;
    std::vector<CCheckBox *> boxes_;
    CTextLabel *message_ = nullptr;

    CMultiLineTextLabel *text(const CRect &r, const CColor &color, CFontRef font)
    {
        auto *l = new CMultiLineTextLabel(r);
        l->setFontColor(color);
        l->setFont(font);
        l->setHoriAlign(kLeftText);
        l->setTransparency(true);
        l->setLineLayout(CMultiLineTextLabel::LineLayout::wrap);
        addView(l);
        return l;
    }

    void build()
    {
        auto title = makeOwned<CFontDesc>(*kSystemFont);
        title->setSize(16);
        title->setStyle(kBoldFace);
        text(CRect(M, 14, W - M - 100, 38), kText, title)->setText("Options");
        addView(new CTextButton(CRect(W - M - 90, 12, W - M, 38), this, kBack_, "Back"));
        text(CRect(M, 46, W - M, 80), kDim, kNormalFontSmall)
            ->setText("The plug-in reads these when it starts: a change applies from the next start of the plug-in "
                      "(reload it or restart the DAW). Nothing here is sent to the Virus.");
        const auto &opts = vti::cc::plugin_options();
        CCoord y = 88;
        for (size_t i = 0; i < opts.size(); i++, y += kRow) {
            auto *box = new CCheckBox(CRect(M, y, W - M, y + 22), this, kFirstOption + (int)i, opts[i].title);
            box->setFontColor(kText);
            box->setFont(kNormalFont);
            addView(box);
            boxes_.push_back(box);
            text(CRect(M + 24, y + 22, W - M, y + kRow - 4), kDim, kNormalFontSmall)->setText(opts[i].hint);
        }
        message_ = new CTextLabel(CRect(M, H - 34, W - M, H - 12));
        message_->setFontColor(kAccent);
        message_->setFont(kNormalFontSmall);
        message_->setHoriAlign(kLeftText);
        message_->setTransparency(true);
        addView(message_);
    }

    void refresh()
    {
        const auto &opts = vti::cc::plugin_options();
        for (size_t i = 0; i < boxes_.size() && i < opts.size(); i++)
            boxes_[i]->setValue(vti::cc::plugin_option(opts[i].name) ? 1.f : 0.f);
        invalid();
    }
};

} // namespace

CViewContainer *make_options_page(std::function<void()> back)
{
    auto *p = new OptionsPage(std::move(back));
    p->setVisible(false);
    return p;
}

void set_options_page_active(CViewContainer *page, bool on)
{
    if (auto *p = dynamic_cast<OptionsPage *>(page))
        p->activate(on);
}

} // namespace tirevive
