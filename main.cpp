#include <chrono>
#include <thread>
#include <QCoreApplication>
#include <QtSql>
#include <QDebug>
#include <tgbot/tgbot.h>
#include <memory>
#include <map>

std::map<int64_t, std::string> userStates; //Структура для отслеживания состояний пользователя
std::map<int64_t, std::string> tempData; //Для хранения названий дома, растения и т.п.

void sendMainMenu(const TgBot::Bot& bot, int64_t chatId) {
    TgBot::ReplyKeyboardMarkup::Ptr keyboard(new TgBot::ReplyKeyboardMarkup);
    keyboard->resizeKeyboard = true;

    std::vector<TgBot::KeyboardButton::Ptr> row1;
    auto btnAddHouse = std::make_shared<TgBot::KeyboardButton>();
    btnAddHouse->text = "Добавить дом";
    row1.push_back(btnAddHouse);

    auto btnListHouses = std::make_shared<TgBot::KeyboardButton>();
    btnListHouses->text = "Список домов";
    row1.push_back(btnListHouses);

    std::vector<TgBot::KeyboardButton::Ptr> row2;
    auto btnAddPlant = std::make_shared<TgBot::KeyboardButton>();
    btnAddPlant->text = "Добавить растение";
    row2.push_back(btnAddPlant);

    auto btnDeleteHouse = std::make_shared<TgBot::KeyboardButton>();
    btnDeleteHouse->text = "Удалить дом";
    row2.push_back(btnDeleteHouse);

    auto btnListPlants = std::make_shared<TgBot::KeyboardButton>();
    btnListPlants->text = "Список растений";
    row2.push_back(btnListPlants);

    auto btnInfo = std::make_shared<TgBot::KeyboardButton>();
    btnInfo->text = "Информация";
    row2.push_back(btnInfo);

    auto btnMyPlants = std::make_shared<TgBot::KeyboardButton>();
    btnMyPlants->text = "Мои растения";
    std::vector<TgBot::KeyboardButton::Ptr> row3;
    row3.push_back(btnMyPlants);

    auto btnDeletePlant = std::make_shared<TgBot::KeyboardButton>();
    btnDeletePlant->text = "Удалить растение";
    std::vector<TgBot::KeyboardButton::Ptr> row4;
    row4.push_back(btnDeletePlant);

    keyboard->keyboard.push_back(row4);
    keyboard->keyboard.push_back(row3);
    keyboard->keyboard.push_back(row1);
    keyboard->keyboard.push_back(row2);



    bot.getApi().sendMessage(chatId, "Выберите действие:", false, 0, keyboard);
}

int main(int argc, char *argv[]) {
    QCoreApplication a(argc, argv);


    //Подключение к базе данных
    QSqlDatabase db = QSqlDatabase::addDatabase("QSQLITE");
    db.setDatabaseName("C:/sqlite/project.db");
    if (!db.open()) {
        qDebug() << "Не удалось открыть базу данных:" << db.lastError().text();
        return 1;
    }

    TgBot::Bot bot("8111615890:AAHzSN9jpC1zfr_DcOvYTRSYyW_wpQzGwy8");

    //Уведомление о поливе
    std::thread([&bot, &db]() {
        while (true) {
            QSqlQuery q(db);
            if (!q.exec("SELECT plants.plant_name, plants.house_name, plants.user_id, plants.last_watered, poliv.min_time, poliv.max_time "
                        "FROM plants "
                        "JOIN main ON plants.plant_name = main.Flower_name "
                        "JOIN poliv ON main.id_plants = poliv.id_plants "
                        "WHERE plants.notified = 0 "
                        "AND (plants.next_remind_time IS NULL OR plants.next_remind_time <= datetime('now'))"))
            {
                qDebug() << "[Ошибка SQL]" << q.lastError().text();
                std::this_thread::sleep_for(std::chrono::minutes(1));
                continue;
            }

            qint64 now = QDateTime::currentSecsSinceEpoch();

            while (q.next()) {
                QString plant = q.value(0).toString();
                QString house = q.value(1).toString();
                qint64 userId = q.value(2).toLongLong();
                qint64 last = QDateTime::fromString(q.value(3).toString(), Qt::ISODate).toSecsSinceEpoch();
                int minT = q.value(4).toInt();
                int maxT = q.value(5).toInt();
                int mid = (minT + maxT) / 2;

                if (now >= last + mid * 3600) {
                    TgBot::InlineKeyboardMarkup::Ptr keyboard(new TgBot::InlineKeyboardMarkup);
                    std::vector<std::vector<TgBot::InlineKeyboardButton::Ptr>> rows;

                    auto btn1 = std::make_shared<TgBot::InlineKeyboardButton>();
                    btn1->text = "Полил";
                    btn1->callbackData = "watered:" + plant.toStdString() + "|" + house.toStdString();
                    rows.push_back({btn1});

                    auto btn2 = std::make_shared<TgBot::InlineKeyboardButton>();
                    btn2->text = "Напомнить позже";
                    btn2->callbackData = "remind:" + plant.toStdString() + "|" + house.toStdString();
                    rows.push_back({btn2});

                    keyboard->inlineKeyboard = rows;
                    bot.getApi().sendMessage(userId,
                                             QString::fromStdString("Пора полить растение \"%1\" в доме \"%2\".").arg(plant, house).toStdString(),false, 0, keyboard);

                    QSqlQuery mark(db);
                    mark.prepare("UPDATE plants SET notified = 1 WHERE plant_name = ? AND house_name = ? AND user_id = ?");
                    mark.addBindValue(plant);
                    mark.addBindValue(house);
                    mark.addBindValue(userId);
                    mark.exec();
                }
            }

            std::this_thread::sleep_for(std::chrono::minutes(1));
        }
    }).detach();

    //Главное меню
    bot.getEvents().onCommand("start", [&bot](TgBot::Message::Ptr msg) {
        sendMainMenu(bot, msg->chat->id);
    });


    bot.getEvents().onCallbackQuery([&bot, &db](TgBot::CallbackQuery::Ptr query) {
        int64_t chatId = query->message->chat->id;
        std::string data = query->data;

        if (data.rfind("del_all_plants:", 0) == 0) {
            std::string house = data.substr(15);

            QSqlQuery q(db);
            q.prepare("DELETE FROM plants WHERE house_name = ? AND user_id = ?");
            q.addBindValue(QString::fromStdString(house));
            q.addBindValue(static_cast<qint64>(chatId));

            if (q.exec()) {
                bot.getApi().sendMessage(chatId, "Все растения из дома \"" + house + "\" удалены.");
            } else {
                bot.getApi().sendMessage(chatId, "Ошибка при удалении всех растений: " + q.lastError().text().toStdString());
            }

            sendMainMenu(bot, chatId);
            return;
        }


        if (data.rfind("del_plant:", 0) == 0) {
            std::string payload = data.substr(10);
            auto sep = payload.find('|');
            std::string plant = payload.substr(0, sep);
            std::string house = payload.substr(sep + 1);

            QSqlQuery q(db);
            q.prepare("DELETE FROM plants WHERE plant_name = ? AND house_name = ? AND user_id = ?");
            q.addBindValue(QString::fromStdString(plant));
            q.addBindValue(QString::fromStdString(house));
            q.addBindValue(static_cast<qint64>(chatId));

            if (q.exec()) {
                bot.getApi().sendMessage(chatId, "Растение \"" + plant + "\" удалено из дома \"" + house + "\".");
            } else {
                bot.getApi().sendMessage(chatId, "Ошибка при удалении: " + q.lastError().text().toStdString());
            }

            sendMainMenu(bot, chatId);
            return;
        }


        if (data.rfind("delplant_house:", 0) == 0) {
            std::string house = data.substr(15);
            QSqlQuery q(db);
            q.prepare("SELECT plant_name FROM plants WHERE house_name = ? AND user_id = ?");
            q.addBindValue(QString::fromStdString(house));
            q.addBindValue(static_cast<qint64>(chatId));

            TgBot::InlineKeyboardMarkup::Ptr keyboard(new TgBot::InlineKeyboardMarkup);
            std::vector<std::vector<TgBot::InlineKeyboardButton::Ptr>> rows;

            //Кнопки для каждого растения
            if (!q.exec()) {
                qDebug() << "Ошибка при SELECT plant_name:" << q.lastError().text();
                return;
            }
            while (q.next()) {
                QString plant = q.value(0).toString();
                if (!plant.trimmed().isEmpty()) {
                    auto btn = std::make_shared<TgBot::InlineKeyboardButton>();
                    btn->text = "" + plant.toStdString();
                    btn->callbackData = "del_plant:" + plant.toStdString() + "|" + house;
                    rows.push_back({btn});
                    qDebug() << "[Добавлена кнопка]" << plant;
                }
            }

            //Кнопка "Удалить все растения"
            if (!rows.empty()) {
                auto btnDelAll = std::make_shared<TgBot::InlineKeyboardButton>();
                btnDelAll->text = "Удалить все растения";
                btnDelAll->callbackData = "del_all_plants:" + house;
                rows.push_back({btnDelAll});

                keyboard->inlineKeyboard = rows;
                bot.getApi().sendMessage(chatId, "Выберите растение для удаления:", false, 0, keyboard);
            } else {
                bot.getApi().sendMessage(chatId, "В этом доме нет растений.");
            }

            return;
        }


        if (data.rfind("myplants_house:", 0) == 0) {
            std::string house = data.substr(15);
            QSqlQuery q(db);
            qDebug() << "chatId:" << chatId;
            qDebug() << "house (from callback):" << QString::fromStdString(house);
            QSqlQuery debugQ(db);
            debugQ.exec("SELECT plant_name, house_name, user_id FROM plants");

            q.prepare("SELECT plant_name FROM plants WHERE house_name = ? AND user_id = ?");
            q.addBindValue(QString::fromStdString(house));
            q.addBindValue(static_cast<qint64>(chatId));

            QString res;
            if (q.exec()) {
                int count = 0;
                while (q.next()) {
                    res += q.value(0).toString() + "\n";
                    count++;
                }

                if (count == 0) {
                    bot.getApi().sendMessage(chatId, "В этом доме нет растений.");
                } else {
                    bot.getApi().sendMessage(chatId, "Растения в доме \"" + house + "\":\n" + res.toStdString());
                }
            } else {
                bot.getApi().sendMessage(chatId, "Ошибка запроса: " + q.lastError().text().toStdString());
            }

            sendMainMenu(bot, chatId);
            return;
        }

        if (data.rfind("info_plant:", 0) == 0) {
            std::string plant = data.substr(11);
            QSqlQuery q(db);
            q.prepare("SELECT info_plants FROM info_about_plants "
                      "JOIN main ON info_about_plants.id_plants = main.id_plants "
                      "WHERE Flower_name = ?");
            q.addBindValue(QString::fromStdString(plant));

            if (q.exec() && q.next()) {
                bot.getApi().sendMessage(chatId, q.value(0).toString().toStdString());
            } else {
                bot.getApi().sendMessage(chatId, "Информация не найдена.");
            }

            sendMainMenu(bot, chatId);
            userStates[chatId] = "";
        }

        if (data.rfind("watering_plant:", 0) == 0) {
            std::string plant = data.substr(15);
            QSqlQuery q(db);
            q.prepare("SELECT min_time, max_time FROM poliv "
                      "JOIN main ON poliv.id_plants = main.id_plants "
                      "WHERE Flower_name = ?");
            q.addBindValue(QString::fromStdString(plant));
            if (q.exec() && q.next()) {
                int minTime = q.value(0).toInt();
                int maxTime = q.value(1).toInt();
                bot.getApi().sendMessage(chatId,"Вы должны поливать растение не раньше чем через " +std::to_string(minTime) + " часов и не позже чем через " +std::to_string(maxTime) + " часов.");
            } else {
                bot.getApi().sendMessage(chatId, "Информация о поливе не найдена.");
            }

            userStates[chatId] = "";
            sendMainMenu(bot, chatId);
        }

        if (data.rfind("care_plant:", 0) == 0) {
            std::string plant = data.substr(11);
            QSqlQuery q(db);
            q.prepare("SELECT recomend FROM info_about_plants "
                      "JOIN main ON info_about_plants.id_plants = main.id_plants "
                      "WHERE Flower_name = ?");
            q.addBindValue(QString::fromStdString(plant));

            if (q.exec() && q.next()) {
                bot.getApi().sendMessage(chatId, q.value(0).toString().toStdString());
            } else {
                bot.getApi().sendMessage(chatId, "Рекомендации не найдены.");
            }
            userStates[chatId] = "";
            sendMainMenu(bot, chatId);
        }

        if (data.rfind("select_plant:", 0) == 0) {
            std::string plant = data.substr(13);
            std::string house = tempData[chatId];
            QSqlQuery q(db);
            q.prepare("INSERT INTO plants (plant_name, house_name, user_id) VALUES (?, ?, ?)");
            q.addBindValue(QString::fromStdString(plant));
            q.addBindValue(QString::fromStdString(house));
            q.addBindValue(static_cast<qint64>(chatId));

            if (q.exec()) {
                bot.getApi().sendMessage(chatId, "Растение \"" + plant + "\" добавлено в дом \"" + house + "\".");
            } else {
                bot.getApi().sendMessage(chatId, "Ошибка: " + q.lastError().text().toStdString());
            }
            userStates[chatId] = "";
            sendMainMenu(bot, chatId);
        }

        if (data == "back_to_categories") {
            TgBot::ReplyKeyboardMarkup::Ptr keyboard(new TgBot::ReplyKeyboardMarkup);
            keyboard->resizeKeyboard = true;

            std::vector<TgBot::KeyboardButton::Ptr> row1, row2, row3, row4;

            auto btn1 = std::make_shared<TgBot::KeyboardButton>();
            btn1->text = "Листовые декоративные растения";
            row1.push_back(btn1);

            auto btn2 = std::make_shared<TgBot::KeyboardButton>();
            btn2->text = "Цветущие комнатные растения";
            row1.push_back(btn2);

            auto btn3 = std::make_shared<TgBot::KeyboardButton>();
            btn3->text = "Кактусы и суккуленты";
            row2.push_back(btn3);

            auto btn4 = std::make_shared<TgBot::KeyboardButton>();
            btn4->text = "Пальмы и крупные растения";
            row2.push_back(btn4);

            auto btn5 = std::make_shared<TgBot::KeyboardButton>();
            btn5->text = "Ароматные и пряные растения на подоконнике";
            row3.push_back(btn5);

            auto btnBack = std::make_shared<TgBot::KeyboardButton>();
            btnBack->text = "Назад";
            row4.push_back(btnBack);

            keyboard->keyboard.push_back(row1);
            keyboard->keyboard.push_back(row2);
            keyboard->keyboard.push_back(row3);
            keyboard->keyboard.push_back(row4);

            bot.getApi().sendMessage(chatId, "Выберите категорию растений:", false, 0, keyboard);
        }

        if (data.rfind("delete:", 0) == 0) {
            std::string house = data.substr(7);
            QSqlQuery q(db);
            q.prepare("DELETE FROM houses WHERE house_name = ? AND user_id = ?");
            q.addBindValue(QString::fromStdString(house));
            q.addBindValue(static_cast<qint64>(chatId));
            q.exec();

            bot.getApi().sendMessage(chatId, "Дом \"" + house + "\" удалён.");
            sendMainMenu(bot, chatId);
        }
        if (data.rfind("plant_house:", 0) == 0) {
            std::string house = data.substr(12);
            tempData[chatId] = house;
            userStates[chatId] = "add_plant";

            TgBot::ReplyKeyboardMarkup::Ptr keyboard(new TgBot::ReplyKeyboardMarkup);
            keyboard->resizeKeyboard = true;

            std::vector<TgBot::KeyboardButton::Ptr> row1;
            auto btn1 = std::make_shared<TgBot::KeyboardButton>();
            btn1->text = "Листовые декоративные растения";
            row1.push_back(btn1);

            auto btn2 = std::make_shared<TgBot::KeyboardButton>();
            btn2->text = "Цветущие комнатные растения";
            row1.push_back(btn2);

            std::vector<TgBot::KeyboardButton::Ptr> row2;
            auto btn3 = std::make_shared<TgBot::KeyboardButton>();
            btn3->text = "Кактусы и суккуленты";
            row2.push_back(btn3);

            auto btn4 = std::make_shared<TgBot::KeyboardButton>();
            btn4->text = "Пальмы и крупные растения";
            row2.push_back(btn4);

            std::vector<TgBot::KeyboardButton::Ptr> row3;
            auto btn5 = std::make_shared<TgBot::KeyboardButton>();
            btn5->text = "Ароматные и пряные растения на подоконнике";
            row3.push_back(btn5);

            keyboard->keyboard.push_back(row1);
            keyboard->keyboard.push_back(row2);
            keyboard->keyboard.push_back(row3);

            userStates[chatId] = "choose_plant_type";

            bot.getApi().sendMessage(chatId, "Выберите категорию растения для дома \"" + house + "\":", false, 0, keyboard);

        }
        if (query->data.rfind("watered:", 0) == 0) {
            std::string payload = query->data.substr(8);
            size_t sep = payload.find("|");
            std::string plant = payload.substr(0, sep);
            std::string house = payload.substr(sep + 1);
            QSqlQuery update(db);
            update.prepare("UPDATE plants SET last_watered = ?, notified = 0 WHERE plant_name = ? AND house_name = ? AND user_id = ?");
            update.addBindValue(QDateTime::currentDateTime().toString(Qt::ISODate));
            update.addBindValue(QString::fromStdString(plant));
            update.addBindValue(QString::fromStdString(house));
            update.addBindValue(static_cast<qint64>(query->message->chat->id));
            update.exec();
            bot.getApi().sendMessage(query->message->chat->id, "Отлично! Таймер полива для \"" + plant + "\" сброшен.");
            return;
        }
        if (query->data.rfind("remind:", 0) == 0) {
            std::string payload = query->data.substr(7);
            size_t sep = payload.find("|");
            std::string plant = payload.substr(0, sep);
            std::string house = payload.substr(sep + 1);
            QSqlQuery update(db);
            update.prepare("UPDATE plants SET next_remind_time = datetime('now', '+90 seconds'), notified = 0 "
                           "WHERE plant_name = ? AND house_name = ? AND user_id = ?");
            update.addBindValue(QString::fromStdString(plant));
            update.addBindValue(QString::fromStdString(house));
            update.addBindValue(static_cast<qint64>(query->message->chat->id));
            update.exec();

            bot.getApi().sendMessage(query->message->chat->id, "Хорошо, напомню через 1.5 минуты.");
            return;
        }

    });

    //Кнопки меню
    bot.getEvents().onAnyMessage([&bot, &db](TgBot::Message::Ptr msg) {
        std::string text = msg->text;
        int64_t chatId = msg->chat->id;

        if (text == "Удалить растение") {
            QSqlQuery q(db);
            q.prepare("SELECT house_name FROM houses WHERE user_id = ?");
            q.addBindValue(static_cast<qint64>(chatId));

            if (q.exec()) {
                TgBot::InlineKeyboardMarkup::Ptr keyboard(new TgBot::InlineKeyboardMarkup);
                std::vector<std::vector<TgBot::InlineKeyboardButton::Ptr>> rows;

                while (q.next()) {
                    QString name = q.value(0).toString();
                    auto btn = std::make_shared<TgBot::InlineKeyboardButton>();
                    btn->text = name.toStdString();
                    btn->callbackData = "delplant_house:" + name.toStdString();
                    rows.push_back({btn});
                }
                if (rows.empty()) {
                    bot.getApi().sendMessage(chatId, "У вас нет домов.");
                    sendMainMenu(bot, chatId);
                    return;
                }
                keyboard->inlineKeyboard = rows;
                bot.getApi().sendMessage(chatId, "Выберите дом, из которого удалить растение:", false, 0, keyboard);
            }

            return;
        }

        if (text == "Мои растения") {
            QSqlQuery q(db);
            q.prepare("SELECT house_name FROM houses WHERE user_id = ?");
            q.addBindValue(static_cast<qint64>(chatId));

            if (q.exec()) {
                TgBot::InlineKeyboardMarkup::Ptr keyboard(new TgBot::InlineKeyboardMarkup);
                std::vector<std::vector<TgBot::InlineKeyboardButton::Ptr>> rows;

                while (q.next()) {
                    QString houseName = q.value(0).toString();
                    auto btn = std::make_shared<TgBot::InlineKeyboardButton>();
                    btn->text = houseName.toStdString();
                    btn->callbackData = "myplants_house:" + houseName.toStdString();
                    rows.push_back({btn});
                }

                if (rows.empty()) {
                    bot.getApi().sendMessage(chatId, "У вас нет добавленных домов.");
                } else {
                    keyboard->inlineKeyboard = rows;
                    bot.getApi().sendMessage(chatId, "Выберите дом, чтобы посмотреть растения:", false, 0, keyboard);
                }
            } else {
                bot.getApi().sendMessage(chatId, "Ошибка при получении списка домов: " + q.lastError().text().toStdString());
            }

            return;
        }

        if (text == "Информация о растении") {
            userStates[chatId] = "choose_plant_info_category";

            TgBot::ReplyKeyboardMarkup::Ptr keyboard(new TgBot::ReplyKeyboardMarkup);
            keyboard->resizeKeyboard = true;

            std::vector<TgBot::KeyboardButton::Ptr> row1, row2, row3, row4;

            auto btn1 = std::make_shared<TgBot::KeyboardButton>();
            btn1->text = "Листовые декоративные растения"; row1.push_back(btn1);

            auto btn2 = std::make_shared<TgBot::KeyboardButton>();
            btn2->text = "Цветущие комнатные растения"; row1.push_back(btn2);

            auto btn3 = std::make_shared<TgBot::KeyboardButton>();
            btn3->text = "Кактусы и суккуленты"; row2.push_back(btn3);

            auto btn4 = std::make_shared<TgBot::KeyboardButton>();
            btn4->text = "Пальмы и крупные растения"; row2.push_back(btn4);

            auto btn5 = std::make_shared<TgBot::KeyboardButton>();
            btn5->text = "Ароматные и пряные растения на подоконнике"; row3.push_back(btn5);

            auto btnBack = std::make_shared<TgBot::KeyboardButton>();
            btnBack->text = "Назад"; row4.push_back(btnBack);

            keyboard->keyboard.push_back(row1);
            keyboard->keyboard.push_back(row2);
            keyboard->keyboard.push_back(row3);
            keyboard->keyboard.push_back(row4);

            bot.getApi().sendMessage(chatId, "Выберите категорию растения:", false, 0, keyboard);
            return;
        }

        if (text == "О поливе") {
            userStates[chatId] = "choose_watering_category";

            TgBot::ReplyKeyboardMarkup::Ptr keyboard(new TgBot::ReplyKeyboardMarkup);
            keyboard->resizeKeyboard = true;

            std::vector<TgBot::KeyboardButton::Ptr> row1, row2, row3, row4;

            auto btn1 = std::make_shared<TgBot::KeyboardButton>();
            btn1->text = "Листовые декоративные растения"; row1.push_back(btn1);

            auto btn2 = std::make_shared<TgBot::KeyboardButton>();
            btn2->text = "Цветущие комнатные растения"; row1.push_back(btn2);

            auto btn3 = std::make_shared<TgBot::KeyboardButton>();
            btn3->text = "Кактусы и суккуленты"; row2.push_back(btn3);

            auto btn4 = std::make_shared<TgBot::KeyboardButton>();
            btn4->text = "Пальмы и крупные растения"; row2.push_back(btn4);

            auto btn5 = std::make_shared<TgBot::KeyboardButton>();
            btn5->text = "Ароматные и пряные растения на подоконнике"; row3.push_back(btn5);

            auto btnBack = std::make_shared<TgBot::KeyboardButton>();
            btnBack->text = "Назад"; row4.push_back(btnBack);

            keyboard->keyboard.push_back(row1);
            keyboard->keyboard.push_back(row2);
            keyboard->keyboard.push_back(row3);
            keyboard->keyboard.push_back(row4);

            bot.getApi().sendMessage(chatId, "Выберите категорию растения:", false, 0, keyboard);
            return;
        }

        if (text == "Рекомендации по уходу") {
            userStates[chatId] = "choose_care_category";

            TgBot::ReplyKeyboardMarkup::Ptr keyboard(new TgBot::ReplyKeyboardMarkup);
            keyboard->resizeKeyboard = true;

            std::vector<TgBot::KeyboardButton::Ptr> row1, row2, row3, row4;

            auto btn1 = std::make_shared<TgBot::KeyboardButton>();
            btn1->text = "Листовые декоративные растения"; row1.push_back(btn1);

            auto btn2 = std::make_shared<TgBot::KeyboardButton>();
            btn2->text = "Цветущие комнатные растения"; row1.push_back(btn2);

            auto btn3 = std::make_shared<TgBot::KeyboardButton>();
            btn3->text = "Кактусы и суккуленты"; row2.push_back(btn3);

            auto btn4 = std::make_shared<TgBot::KeyboardButton>();
            btn4->text = "Пальмы и крупные растения"; row2.push_back(btn4);

            auto btn5 = std::make_shared<TgBot::KeyboardButton>();
            btn5->text = "Ароматные и пряные растения на подоконнике"; row3.push_back(btn5);

            auto btnBack = std::make_shared<TgBot::KeyboardButton>();
            btnBack->text = "Назад"; row4.push_back(btnBack);

            keyboard->keyboard.push_back(row1);
            keyboard->keyboard.push_back(row2);
            keyboard->keyboard.push_back(row3);
            keyboard->keyboard.push_back(row4);

            bot.getApi().sendMessage(chatId, "Выберите категорию растения:", false, 0, keyboard);
            return;
        }

        if (userStates[chatId] == "choose_watering_category") {
            std::map<std::string, int> typeMap = {
                {"Листовые декоративные растения", 1},
                {"Цветущие комнатные растения", 2},
                {"Кактусы и суккуленты", 3},
                {"Пальмы и крупные растения", 4},
                {"Ароматные и пряные растения на подоконнике", 5}
            };

            if (typeMap.count(text)) {
                int idType = typeMap[text];
                tempData[chatId] = std::to_string(idType);
                QSqlQuery q(db);
                q.prepare("SELECT Flower_name FROM main WHERE id_type = ?");
                q.addBindValue(idType);
                if (q.exec()) {
                    TgBot::InlineKeyboardMarkup::Ptr keyboard(new TgBot::InlineKeyboardMarkup);
                    std::vector<std::vector<TgBot::InlineKeyboardButton::Ptr>> rows;
                    while (q.next()) {
                        QString plant = q.value(0).toString();
                        auto btn = std::make_shared<TgBot::InlineKeyboardButton>();
                        btn->text = plant.toStdString();
                        btn->callbackData = "watering_plant:" + plant.toStdString();
                        rows.push_back({btn});
                    }

                    keyboard->inlineKeyboard = rows;
                    bot.getApi().sendMessage(chatId, "Выберите растение:", false, 0, keyboard);
                    userStates[chatId] = "waiting_for_watering_info";
                }
                return;
            }
        }

        if (userStates[chatId] == "choose_care_category") {
            std::map<std::string, int> typeMap = {
                {"Листовые декоративные растения", 1},
                {"Цветущие комнатные растения", 2},
                {"Кактусы и суккуленты", 3},
                {"Пальмы и крупные растения", 4},
                {"Ароматные и пряные растения на подоконнике", 5}
            };

            if (typeMap.count(text)) {
                int idType = typeMap[text];
                tempData[chatId] = std::to_string(idType); // сохраняем тип

                QSqlQuery q(db);
                q.prepare("SELECT Flower_name FROM main WHERE id_type = ?");
                q.addBindValue(idType);

                if (q.exec()) {
                    TgBot::InlineKeyboardMarkup::Ptr keyboard(new TgBot::InlineKeyboardMarkup);
                    std::vector<std::vector<TgBot::InlineKeyboardButton::Ptr>> rows;

                    while (q.next()) {
                        QString plant = q.value(0).toString();
                        auto btn = std::make_shared<TgBot::InlineKeyboardButton>();
                        btn->text = plant.toStdString();
                        btn->callbackData = "care_plant:" + plant.toStdString();
                        rows.push_back({btn});
                    }

                    keyboard->inlineKeyboard = rows;
                    bot.getApi().sendMessage(chatId, "Выберите растение:", false, 0, keyboard);
                    userStates[chatId] = "waiting_for_care_info";
                }
                return;
            }
        }

        if (userStates[chatId] == "choose_plant_info_category") {
            std::map<std::string, int> typeMap = {
                {"Листовые декоративные растения", 1},
                {"Цветущие комнатные растения", 2},
                {"Кактусы и суккуленты", 3},
                {"Пальмы и крупные растения", 4},
                {"Ароматные и пряные растения на подоконнике", 5}
            };

            if (typeMap.count(text)) {
                int idType = typeMap[text];
                tempData[chatId] = std::to_string(idType); // сохранить выбранный тип

                QSqlQuery q(db);
                q.prepare("SELECT Flower_name FROM main WHERE id_type = ?");
                q.addBindValue(idType);

                if (q.exec()) {
                    TgBot::InlineKeyboardMarkup::Ptr keyboard(new TgBot::InlineKeyboardMarkup);
                    std::vector<std::vector<TgBot::InlineKeyboardButton::Ptr>> rows;

                    while (q.next()) {
                        QString plant = q.value(0).toString();
                        auto btn = std::make_shared<TgBot::InlineKeyboardButton>();
                        btn->text = plant.toStdString();
                        btn->callbackData = "info_plant:" + plant.toStdString();
                        rows.push_back({btn});
                    }

                    if (!rows.empty()) {
                        keyboard->inlineKeyboard = rows;
                        bot.getApi().sendMessage(chatId, "Выберите растение:", false, 0, keyboard);
                        userStates[chatId] = "waiting_for_plant_info";
                    } else {
                        bot.getApi().sendMessage(chatId, "Растения не найдены для этой категории.");
                        sendMainMenu(bot, chatId);
                        userStates[chatId] = "";
                    }
                }
                return;
            }
        }


        if (text == "Информация") {
            userStates[chatId] = "info_menu";

            TgBot::ReplyKeyboardMarkup::Ptr keyboard(new TgBot::ReplyKeyboardMarkup);
            keyboard->resizeKeyboard = true;

            std::vector<TgBot::KeyboardButton::Ptr> row1;
            auto btnTypes = std::make_shared<TgBot::KeyboardButton>();
            btnTypes->text = "Информация о видах";
            row1.push_back(btnTypes);

            std::vector<TgBot::KeyboardButton::Ptr> row2;
            auto btnPlantInfo = std::make_shared<TgBot::KeyboardButton>();
            btnPlantInfo->text = "Информация о растении";
            row2.push_back(btnPlantInfo);

            std::vector<TgBot::KeyboardButton::Ptr> row3;
            auto btnCare = std::make_shared<TgBot::KeyboardButton>();
            btnCare->text = "Рекомендации по уходу";
            row3.push_back(btnCare);

            std::vector<TgBot::KeyboardButton::Ptr> row4;
            auto btnPoliv = std::make_shared<TgBot::KeyboardButton>();
            btnPoliv->text = "О поливе";
            row4.push_back(btnPoliv);

            std::vector<TgBot::KeyboardButton::Ptr> row5;
            auto btnBack = std::make_shared<TgBot::KeyboardButton>();
            btnBack->text = "Назад";
            row5.push_back(btnBack);

            keyboard->keyboard.push_back(row1);
            keyboard->keyboard.push_back(row2);
            keyboard->keyboard.push_back(row3);
            keyboard->keyboard.push_back(row4);
            keyboard->keyboard.push_back(row5);

            bot.getApi().sendMessage(chatId, "Выберите тип информации:", false, 0, keyboard);
            return;
        }

        if (userStates[chatId] == "view_plant_info") {
            QSqlQuery q(db);
            q.prepare("SELECT info_plants FROM info_about_plants "
                      "JOIN main ON info_about_plants.id_plants = main.id_plants "
                      "WHERE Flower_name = ?");
            q.addBindValue(QString::fromStdString(text));

            if (q.exec() && q.next()) {
                bot.getApi().sendMessage(chatId, q.value(0).toString().toStdString());
            } else {
                bot.getApi().sendMessage(chatId, "Информация о растении не найдена.");
            }

            sendMainMenu(bot, chatId);
            userStates[chatId] = "";
            return;
        }

        if (text == "Рекомендации по уходу") {
            userStates[chatId] = "plant_care";
            bot.getApi().sendMessage(chatId, "Введите название растения:");
            return;
        }
        if (userStates[chatId] == "plant_care") {
            QSqlQuery q(db);
            q.prepare("SELECT recomend FROM info_about_plants "
                      "JOIN main ON info_about_plants.id_plants = main.id_plants "
                      "WHERE Flower_name = ?");
            q.addBindValue(QString::fromStdString(text));

            if (q.exec() && q.next()) {
                bot.getApi().sendMessage(chatId, q.value(0).toString().toStdString());
            } else {
                bot.getApi().sendMessage(chatId, "Рекомендации не найдены.");
            }

            sendMainMenu(bot, chatId);
            userStates[chatId] = "";
            return;
        }

        if (text == "О поливе") {
            userStates[chatId] = "watering_info";
            bot.getApi().sendMessage(chatId, "Введите название растения:");
            return;
        }
        if (userStates[chatId] == "watering_info") {
            QSqlQuery q(db);
            q.prepare("SELECT min_time, max_time FROM poliv "
                      "JOIN main ON poliv.id_plants = main.id_plants "
                      "WHERE Flower_name = ?");
            q.addBindValue(QString::fromStdString(text));

            if (q.exec() && q.next()) {
                int minTime = q.value(0).toInt();
                int maxTime = q.value(1).toInt();
                bot.getApi().sendMessage(chatId,
                                         "Вы должны поливать растение не раньше чем через " +
                                             std::to_string(minTime) + " дней, и не позже чем через " +
                                             std::to_string(maxTime) + " дней.");
            } else {
                bot.getApi().sendMessage(chatId, "Данных о поливе не найдено.");
            }

            sendMainMenu(bot, chatId);
            userStates[chatId] = "";
            return;
        }

        if (text == "Информация о видах") {
            userStates[chatId] = "view_type_info";

            TgBot::ReplyKeyboardMarkup::Ptr keyboard(new TgBot::ReplyKeyboardMarkup);
            keyboard->resizeKeyboard = true;

            std::vector<TgBot::KeyboardButton::Ptr> row1;
            auto btn1 = std::make_shared<TgBot::KeyboardButton>();
            btn1->text = "Листовые декоративные растения";
            row1.push_back(btn1);

            auto btn2 = std::make_shared<TgBot::KeyboardButton>();
            btn2->text = "Цветущие комнатные растения";
            row1.push_back(btn2);

            std::vector<TgBot::KeyboardButton::Ptr> row2;
            auto btn3 = std::make_shared<TgBot::KeyboardButton>();
            btn3->text = "Кактусы и суккуленты";
            row2.push_back(btn3);

            auto btn4 = std::make_shared<TgBot::KeyboardButton>();
            btn4->text = "Пальмы и крупные растения";
            row2.push_back(btn4);

            std::vector<TgBot::KeyboardButton::Ptr> row3;
            auto btn5 = std::make_shared<TgBot::KeyboardButton>();
            btn5->text = "Ароматные и пряные растения на подоконнике";
            row3.push_back(btn5);

            std::vector<TgBot::KeyboardButton::Ptr> row4;
            auto btnBack = std::make_shared<TgBot::KeyboardButton>();
            btnBack->text = "Назад";
            row4.push_back(btnBack);

            keyboard->keyboard.push_back(row1);
            keyboard->keyboard.push_back(row2);
            keyboard->keyboard.push_back(row3);
            keyboard->keyboard.push_back(row4);

            bot.getApi().sendMessage(chatId, "Выберите вид растений, чтобы получить информацию:", false, 0, keyboard);
            return;
        }


        //Добавить дом
        if (text == "Добавить дом") {
            userStates[chatId] = "add_house";
            bot.getApi().sendMessage(chatId, "Введите название нового дома:");
            return;
        }
        //Назад
        if (text == "Назад") {
            sendMainMenu(bot, chatId);
            return;
        }


        //Список домов
        if (text == "Список домов") {
            QSqlQuery q(db);
            QString res;

            q.prepare("SELECT house_name FROM houses WHERE user_id = ?");
            q.addBindValue(static_cast<qint64>(chatId));

            if (q.exec()) {
                int count = 0;
                while (q.next()) {
                    res += q.value(0).toString() + "\n";
                    ++count;
                }
                if (count == 0) res = "У вас нет добавленных домов.";
            } else {
                res = "Ошибка запроса к базе: " + q.lastError().text();
            }

            bot.getApi().sendMessage(chatId, res.toStdString());
            sendMainMenu(bot, chatId);
            return;
        }



        // Добавить растение
        if (text == "Добавить растение") {
            QSqlQuery q(db);
            q.prepare("SELECT house_name FROM houses WHERE user_id = ?");
            q.addBindValue(static_cast<qint64>(chatId));
            if (q.exec()) {
                TgBot::InlineKeyboardMarkup::Ptr keyboard(new TgBot::InlineKeyboardMarkup);
                std::vector<std::vector<TgBot::InlineKeyboardButton::Ptr>> rows;
                while (q.next()) {
                    QString name = q.value(0).toString();
                    auto btn = std::make_shared<TgBot::InlineKeyboardButton>();
                    btn->text = name.toStdString();
                    btn->callbackData = "plant_house:" + name.toStdString();
                    rows.push_back({btn});
                }

                if (rows.empty()) {
                    bot.getApi().sendMessage(chatId, "У вас нет домов. Сначала добавьте дом.");
                    sendMainMenu(bot, chatId);
                    return;
                }
                keyboard->inlineKeyboard = rows;
                bot.getApi().sendMessage(chatId, "Выберите дом, куда добавить растение:", false, 0, keyboard);
                return;
            }
        }
        if (text == "Добавить растение") {
            userStates[chatId] = "add_plant";
            bot.getApi().sendMessage(chatId, "Введите название нового растения:");
            return;
        }

        if (text == "Удалить дом") {
            QSqlQuery q(db);
            q.prepare("SELECT house_name FROM houses WHERE user_id = ?");
            q.addBindValue(static_cast<qint64>(chatId));

            if (q.exec()) {
                TgBot::InlineKeyboardMarkup::Ptr keyboard(new TgBot::InlineKeyboardMarkup);
                std::vector<std::vector<TgBot::InlineKeyboardButton::Ptr>> rows;

                while (q.next()) {
                    QString name = q.value(0).toString();
                    auto btn = std::make_shared<TgBot::InlineKeyboardButton>();
                    btn->text = name.toStdString();
                    btn->callbackData = "delete:" + name.toStdString(); // ⚠️ передаём имя

                    rows.push_back({btn});
                }

                if (rows.empty()) {
                    bot.getApi().sendMessage(chatId, "У вас нет домов для удаления.");
                    sendMainMenu(bot, chatId);
                    return;
                }

                keyboard->inlineKeyboard = rows;
                bot.getApi().sendMessage(chatId, "Выберите дом для удаления:", false, 0, keyboard);
                return;
            }
        }
        if (userStates[chatId] == "view_type_info") {
            std::map<std::string, int> typeMap = {
                {"Листовые декоративные растения", 1},
                {"Цветущие комнатные растения", 2},
                {"Кактусы и суккуленты", 3},
                {"Пальмы и крупные растения", 4},
                {"Ароматные и пряные растения на подоконнике", 5}
            };

            if (typeMap.count(text)) {
                int id = typeMap[text];
                QSqlQuery q(db);
                q.prepare("SELECT info_species FROM info_type WHERE id_type = ?");
                q.addBindValue(id);

                if (q.exec() && q.next()) {
                    QString info = q.value(0).toString();
                    bot.getApi().sendMessage(chatId, info.toStdString());
                } else {
                    bot.getApi().sendMessage(chatId, "Информация не найдена.");
                }

                sendMainMenu(bot, chatId);
                userStates[chatId] = "";
                return;
            }
        }


        if (text == "Список растений") {
            TgBot::ReplyKeyboardMarkup::Ptr keyboard(new TgBot::ReplyKeyboardMarkup);
            keyboard->resizeKeyboard = true;

            std::vector<TgBot::KeyboardButton::Ptr> row1;
            auto btn1 = std::make_shared<TgBot::KeyboardButton>();
            btn1->text = "Листовые декоративные растения";
            row1.push_back(btn1);

            auto btn2 = std::make_shared<TgBot::KeyboardButton>();
            btn2->text = "Цветущие комнатные растения";
            row1.push_back(btn2);

            std::vector<TgBot::KeyboardButton::Ptr> row2;
            auto btn3 = std::make_shared<TgBot::KeyboardButton>();
            btn3->text = "Кактусы и суккуленты";
            row2.push_back(btn3);

            auto btn4 = std::make_shared<TgBot::KeyboardButton>();
            btn4->text = "Пальмы и крупные растения";
            row2.push_back(btn4);

            std::vector<TgBot::KeyboardButton::Ptr> row3;
            auto btn5 = std::make_shared<TgBot::KeyboardButton>();
            btn5->text = "Ароматные и пряные растения на подоконнике";
            row3.push_back(btn5);

            auto btn6 = std::make_shared<TgBot::KeyboardButton>();
            btn6->text = "Весь список";
            row3.push_back(btn6);

            std::vector<TgBot::KeyboardButton::Ptr> row4;
            auto btnBack = std::make_shared<TgBot::KeyboardButton>();
            btnBack->text = "Назад";
            row4.push_back(btnBack);

            keyboard->keyboard.push_back(row1);
            keyboard->keyboard.push_back(row2);
            keyboard->keyboard.push_back(row3);
            keyboard->keyboard.push_back(row4);

            bot.getApi().sendMessage(chatId, "Выберите категорию растений:", false, 0, keyboard);
            return;
        }
        //Категория: Листовые декоративные растения
        if (text == "Листовые декоративные растения") {
            QSqlQuery q(db);
            q.prepare("SELECT Flower_name FROM main WHERE id_type = ?");
            q.addBindValue("1");

            if (q.exec()) {
                if (userStates[chatId] == "choose_plant_type") {
                    TgBot::InlineKeyboardMarkup::Ptr keyboard(new TgBot::InlineKeyboardMarkup);
                    std::vector<std::vector<TgBot::InlineKeyboardButton::Ptr>> rows;

                    while (q.next()) {
                        QString plant = q.value(0).toString();
                        auto btn = std::make_shared<TgBot::InlineKeyboardButton>();
                        btn->text = plant.toStdString();
                        btn->callbackData = "select_plant:" + plant.toStdString();
                        rows.push_back({btn});
                    }

                    // Добавляем кнопку Назад
                    auto backBtn = std::make_shared<TgBot::InlineKeyboardButton>();
                    backBtn->text = "Назад";
                    backBtn->callbackData = "back_to_categories";
                    rows.push_back({backBtn});

                    keyboard->inlineKeyboard = rows;
                    bot.getApi().sendMessage(chatId, "Выберите растение для добавления:", false, 0, keyboard);
                } else {
                    QString res;
                    while (q.next()) {
                        res += q.value(0).toString() + "\n";
                    }
                    bot.getApi().sendMessage(chatId, res.toStdString());
                    sendMainMenu(bot, chatId);
                }
            } else {
                bot.getApi().sendMessage(chatId, "Ошибка базы: " + q.lastError().text().toStdString());
            }
            return;
        }

        //Категория: Цветущие комнатные растения
        if (text == "Цветущие комнатные растения") {
            QSqlQuery q(db);
            q.prepare("SELECT Flower_name FROM main WHERE id_type = ?");
            q.addBindValue("2");

            if (q.exec()) {
                if (userStates[chatId] == "choose_plant_type") {
                    TgBot::InlineKeyboardMarkup::Ptr keyboard(new TgBot::InlineKeyboardMarkup);
                    std::vector<std::vector<TgBot::InlineKeyboardButton::Ptr>> rows;

                    while (q.next()) {
                        QString plant = q.value(0).toString();
                        auto btn = std::make_shared<TgBot::InlineKeyboardButton>();
                        btn->text = plant.toStdString();
                        btn->callbackData = "select_plant:" + plant.toStdString();
                        rows.push_back({btn});
                    }

                    // Добавляем кнопку Назад
                    auto backBtn = std::make_shared<TgBot::InlineKeyboardButton>();
                    backBtn->text = "↩ Назад";
                    backBtn->callbackData = "back_to_categories";
                    rows.push_back({backBtn});

                    keyboard->inlineKeyboard = rows;
                    bot.getApi().sendMessage(chatId, "Выберите растение для добавления:", false, 0, keyboard);
                } else {
                    QString res;
                    while (q.next()) {
                        res += q.value(0).toString() + "\n";
                    }
                    bot.getApi().sendMessage(chatId, res.toStdString());
                    sendMainMenu(bot, chatId);
                }
            } else {
                bot.getApi().sendMessage(chatId, "Ошибка базы: " + q.lastError().text().toStdString());
            }
            return;
        }

        //Категория: Кактусы и суккуленты
        if (text == "Кактусы и суккуленты") {
            QSqlQuery q(db);
            q.prepare("SELECT Flower_name FROM main WHERE id_type = ?");
            q.addBindValue("3");

            if (q.exec()) {
                if (userStates[chatId] == "choose_plant_type") {
                    TgBot::InlineKeyboardMarkup::Ptr keyboard(new TgBot::InlineKeyboardMarkup);
                    std::vector<std::vector<TgBot::InlineKeyboardButton::Ptr>> rows;

                    while (q.next()) {
                        QString plant = q.value(0).toString();
                        auto btn = std::make_shared<TgBot::InlineKeyboardButton>();
                        btn->text = plant.toStdString();
                        btn->callbackData = "select_plant:" + plant.toStdString();
                        rows.push_back({btn});
                    }
                    auto backBtn = std::make_shared<TgBot::InlineKeyboardButton>();
                    backBtn->text = "Назад";
                    backBtn->callbackData = "back_to_categories";
                    rows.push_back({backBtn});

                    keyboard->inlineKeyboard = rows;
                    bot.getApi().sendMessage(chatId, "Выберите растение для добавления:", false, 0, keyboard);
                } else {
                    QString res;
                    while (q.next()) {
                        res += q.value(0).toString() + "\n";
                    }
                    bot.getApi().sendMessage(chatId, res.toStdString());
                    sendMainMenu(bot, chatId);
                }
            } else {
                bot.getApi().sendMessage(chatId, "Ошибка базы: " + q.lastError().text().toStdString());
            }
            return;
        }


        //Категория: Пальмы и крупные растения
        if (text == "Пальмы и крупные растения") {
            QSqlQuery q(db);
            q.prepare("SELECT Flower_name FROM main WHERE id_type = ?");
            q.addBindValue("4");
            if (q.exec()) {
                if (userStates[chatId] == "choose_plant_type") {
                    TgBot::InlineKeyboardMarkup::Ptr keyboard(new TgBot::InlineKeyboardMarkup);
                    std::vector<std::vector<TgBot::InlineKeyboardButton::Ptr>> rows;
                    while (q.next()) {
                        QString plant = q.value(0).toString();
                        auto btn = std::make_shared<TgBot::InlineKeyboardButton>();
                        btn->text = plant.toStdString();
                        btn->callbackData = "select_plant:" + plant.toStdString();
                        rows.push_back({btn});
                    }
                    auto backBtn = std::make_shared<TgBot::InlineKeyboardButton>();
                    backBtn->text = "Назад";
                    backBtn->callbackData = "back_to_categories";
                    rows.push_back({backBtn});

                    keyboard->inlineKeyboard = rows;
                    bot.getApi().sendMessage(chatId, "Выберите растение для добавления:", false, 0, keyboard);
                } else {
                    QString res;
                    while (q.next()) {
                        res += q.value(0).toString() + "\n";
                    }
                    bot.getApi().sendMessage(chatId, res.toStdString());
                    sendMainMenu(bot, chatId);
                }
            } else {
                bot.getApi().sendMessage(chatId, "Ошибка базы: " + q.lastError().text().toStdString());
            }
            return;
        }

        //Категория: Ароматные и пряные растения на подоконнике
        if (text == "Ароматные и пряные растения на подоконнике") {
            QSqlQuery q(db);
            q.prepare("SELECT Flower_name FROM main WHERE id_type = ?");
            q.addBindValue("5");

            if (q.exec()) {
                if (userStates[chatId] == "choose_plant_type") {
                    TgBot::InlineKeyboardMarkup::Ptr keyboard(new TgBot::InlineKeyboardMarkup);
                    std::vector<std::vector<TgBot::InlineKeyboardButton::Ptr>> rows;
                    while (q.next()) {
                        QString plant = q.value(0).toString();
                        auto btn = std::make_shared<TgBot::InlineKeyboardButton>();
                        btn->text = plant.toStdString();
                        btn->callbackData = "select_plant:" + plant.toStdString();
                        rows.push_back({btn});
                    }
                    auto backBtn = std::make_shared<TgBot::InlineKeyboardButton>();
                    backBtn->text = "Назад";
                    backBtn->callbackData = "back_to_categories";
                    rows.push_back({backBtn});

                    keyboard->inlineKeyboard = rows;
                    bot.getApi().sendMessage(chatId, "Выберите растение для добавления:", false, 0, keyboard);
                } else {
                    QString res;
                    while (q.next()) {
                        res += q.value(0).toString() + "\n";
                    }
                    bot.getApi().sendMessage(chatId, res.toStdString());
                    sendMainMenu(bot, chatId);
                }
            } else {
                bot.getApi().sendMessage(chatId, "Ошибка базы: " + q.lastError().text().toStdString());
            }
            return;
        }
        // --- Весь список ---
        if (text == "Весь список") {
            QSqlQuery q(db);
            QString res;
            if (q.exec("SELECT Flower_name FROM main")) {
                while (q.next()) {
                    res += q.value(0).toString() + "\n";
                }
            } else {
                res = "Ошибка базы: " + q.lastError().text();
            }
            bot.getApi().sendMessage(chatId, res.toStdString());
            sendMainMenu(bot, chatId);
            return;
        }
        // --- Список растений ---
        if (text == "Список растений") {
            QSqlQuery q(db);
            QString res;
            if (q.exec("SELECT plant_name FROM plants")) {
                int count = 0;
                while (q.next()) {
                    res += q.value(0).toString() + "\n";
                    ++count;
                }
                if (count == 0) res = "Растения не найдены.";
            } else {
                res = "Ошибка запроса к базе: " + q.lastError().text();
            }
            bot.getApi().sendMessage(chatId, res.toStdString());
            sendMainMenu(bot, chatId);
            return;
        }

        //Добавление дома
        if (userStates[chatId] == "add_house") {
            QString houseName = QString::fromStdString(text);
            qDebug() << "Добавляем дом: " << houseName;
            QSqlQuery q(db);
            q.prepare("INSERT INTO houses (house_name, user_id) VALUES (?, ?)");
            q.addBindValue(houseName);
            q.addBindValue(static_cast<qint64>(chatId));
            if (q.exec()) {
                bot.getApi().sendMessage(chatId, "Дом добавлен!");
            } else {
                qDebug() << "Ошибка SQL: " << q.lastError().text();
                bot.getApi().sendMessage(chatId, "Ошибка: " + q.lastError().text().toStdString());
            }
            userStates[chatId] = "";
            sendMainMenu(bot, chatId);
            return;
        }
        //Добавление растения
        if (userStates[chatId] == "add_plant") {
            QString plantName = QString::fromStdString(text);
            QSqlQuery q(db);
            q.prepare("INSERT INTO plants (plant_name) VALUES (?)");
            q.addBindValue(plantName);
            if (q.exec()) {
                bot.getApi().sendMessage(chatId, "Растение добавлено!");
            } else {
                bot.getApi().sendMessage(chatId, "Ошибка: " + q.lastError().text().toStdString());
            }
            userStates[chatId] = "";
            sendMainMenu(bot, chatId);
            return;
        }

        // Возврат в меню по неизвестному сообщению
        if (msg->text != "/start") {
            bot.getApi().sendMessage(chatId, "Неизвестная команда. Выберите действие из меню:");
            sendMainMenu(bot, chatId);
        }
    });
    //Запуск longPoll
    try {
        TgBot::TgLongPoll longPoll(bot);
        qDebug() << "Бот запущен!";
        while (true) {
            longPoll.start();
        }
    } catch (std::exception &e) {
        qDebug() << "Ошибка:" << e.what();
    }

    return 0;
}
