#include <telnetServer.h>

SerialTelnet::SerialTelnet() : server(23) {}

void SerialTelnet::begin(long baud)
{
    Serial.begin(baud);
    server.begin();
}

void SerialTelnet::println(const String &msg)
{
    Serial.println(msg);
    if (client && client.connected())
    {
        client.println(msg);
    }
}

void SerialTelnet::print(const String &msg)
{
    Serial.print(msg);
    if (client && client.connected())
    {
        client.print(msg);
    }
}

void SerialTelnet::printf(const char *format, ...)
{
    char buf[128];
    va_list args;
    va_start(args, format);
    vsnprintf(buf, sizeof(buf), format, args);
    va_end(args);
    Serial.print(buf);
    if (client && client.connected())
    {
        client.print(buf);
    }
}

void SerialTelnet::handleClient()
{
    // Controleer of een nieuwe client verbinding maakt
    if (server.hasClient())
    {
        // Stop de oude client alleen als hij nog verbonden is
        if (client && client.connected())
        {
            client.stop();
        }

        client = server.accept(); // Accepteer nieuwe verbinding

        // Stuur welkomsbericht en resetreden naar de nieuwe client
        if (client && client.connected())
        {
            client.printf("Verbonden met ESP Telnet Server!\n\r");
            client.printf("Laatste Reset Reden: %s\n\r", ESP.getResetReason().c_str());
        }
    }

    // Controleer of de client nog verbonden is
    if (client && client.connected())
    {
        while (client.available())
        {
            char c = client.read();
            Serial.write(c);  // Stuur ontvangen data naar de seriële monitor
        }
    }
}

