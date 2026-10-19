#pragma once

#include <QImage>
#include <QString>

// Extrai o ícone RT_GROUP_ICON / RT_ICON de um executável PE local.
// Não executa o binário nem chama Wine; dados não confiáveis são limitados.
class PeIconExtractor
{
public:
    static QImage extract(const QString &exePath, int requestedSize = 48);
};
