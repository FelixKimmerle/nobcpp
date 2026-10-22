#include <SFML/Graphics.hpp>
#include <cstdio>
#include <cstring>

int main(int argc, char** argv)
{
    if (argc > 1 && std::strcmp(argv[1], "--headless") == 0)
    {
        sf::Image image({64, 64}, sf::Color::Red);
        std::printf("SFML %d.%d.%d, image %ux%u\n", SFML_VERSION_MAJOR, SFML_VERSION_MINOR,
                    SFML_VERSION_PATCH, image.getSize().x, image.getSize().y);
        return 0;
    }

    sf::RenderWindow window(sf::VideoMode({800, 600}), "nobcpp: SFML");
    window.setFramerateLimit(60);
    sf::CircleShape circle(40.f);
    circle.setFillColor(sf::Color(100, 180, 250));
    sf::Vector2f velocity{4.f, 3.f};

    while (window.isOpen())
    {
        while (const std::optional event = window.pollEvent())
        {
            if (event->is<sf::Event::Closed>())
            {
                window.close();
            }
        }
        sf::Vector2f pos = circle.getPosition() + velocity;
        sf::Vector2f size = sf::Vector2f(window.getSize());
        if (pos.x < 0 || pos.x + 80 > size.x) velocity.x = -velocity.x;
        if (pos.y < 0 || pos.y + 80 > size.y) velocity.y = -velocity.y;
        circle.setPosition(pos);

        window.clear(sf::Color(30, 30, 36));
        window.draw(circle);
        window.display();
    }
}
