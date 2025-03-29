#include <QCoreApplication>
#include "runtime.h"

RunTime::RunTime(QObject *parent)
    : QObject(parent)
{
}

RunTime::~RunTime()
{
}

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);

    RunTime runTime;
    
    return app.exec();
}
