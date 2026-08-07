#include "menu_activity.hpp"
#include <unistd.h>
#include <borealis/views/cells/cell_detail.hpp>

MenuActivity::MenuActivity(FeedActivity* feed) : feed(feed) {}

brls::View* MenuActivity::createContentView()
{
    brls::ScrollingFrame* scroll = new brls::ScrollingFrame();
    
    brls::Box* box = new brls::Box(brls::Axis::COLUMN);
    box->setPadding(24, 24, 24, 24);
    
    // Login
    brls::DetailCell* loginItem = new brls::DetailCell();
    loginItem->setText("Giriş Yap (TikTok PIN)");
    loginItem->setDetailText("Chrome Eklentisinden aldiginiz PIN ile giris yapin");
    loginItem->registerClickAction([this](brls::View*) {
        brls::Application::popActivity();
        feed->openLogin();
        return true;
    });
    box->addView(loginItem);
    
    // Search
    brls::DetailCell* searchItem = new brls::DetailCell();
    searchItem->setText("Arama Yap");
    searchItem->setDetailText("Belli bir hesabi veya etiketi ara");
    searchItem->registerClickAction([this](brls::View*) {
        brls::Application::popActivity();
        feed->openSearch();
        return true;
    });
    box->addView(searchItem);
    
    // Region
    brls::DetailCell* regionItem = new brls::DetailCell();
    regionItem->setText("Bölge Değiştir (Region)");
    regionItem->setDetailText("Farkli ulkelerin tiktok akisini izle");
    regionItem->registerClickAction([this](brls::View*) {
        brls::Application::popActivity();
        feed->cycleRegion();
        return true;
    });
    box->addView(regionItem);
    
    // Refresh
    brls::DetailCell* refreshItem = new brls::DetailCell();
    refreshItem->setText("Akışı Yenile (Refresh)");
    refreshItem->setDetailText("En bastan yeni videolar yukle");
    refreshItem->registerClickAction([this](brls::View*) {
        brls::Application::popActivity();
        feed->restart();
        return true;
    });
    box->addView(refreshItem);
    
    // Logout
    brls::DetailCell* logoutItem = new brls::DetailCell();
    logoutItem->setText("Oturumu Kapat (Logout)");
    logoutItem->setDetailText("Cihazdaki mevcut hesabindan cikis yap");
    logoutItem->registerClickAction([this](brls::View*) {
        unlink("sdmc:/switch/switch-tok.sessionid");
        brls::Application::popActivity();
        feed->restart();
        return true;
    });
    box->addView(logoutItem);
    
    scroll->setContentView(box);

    brls::AppletFrame* frame = new brls::AppletFrame(scroll);
    frame->setTitle("Ayarlar");
    
    return frame;
}
