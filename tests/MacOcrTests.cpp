#include "recognition/PlatformOcr.h"
#include <QApplication>
#include <QPainter>
#include <iostream>
#include <stdexcept>
int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    using namespace singlilt;
    if (recognizeText(QImage()).error.isEmpty())
        throw std::runtime_error("empty OCR");
    QImage image(800, 240, QImage::Format_RGB32);
    image.fill(Qt::white);
    QPainter painter(&image);
    painter.setFont(QFont("Helvetica", 48));
    painter.setPen(Qt::black);
    painter.drawText(40, 130, "SingLilt 123");
    painter.end();
    auto result = recognizeText(image, "en-US");
    if (!result.error.isEmpty() || !result.text.contains("SingLilt", Qt::CaseInsensitive))
        throw std::runtime_error(result.error.toStdString() + result.text.toStdString());
    for (auto &word : result.words)
        if (word.box.x() < 0 || word.box.y() < 0 || word.box.right() > 800 || word.box.bottom() > 240)
            throw std::runtime_error("OCR geometry");
    std::cout << "PASS OCR text and source bounds\n";
}
