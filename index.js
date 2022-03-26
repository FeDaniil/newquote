import bindings from 'bindings';
const cpp_back = bindings('newquote-cpp-backend');
import fs from 'fs';
import fetch from 'node-fetch';
import { VK } from 'vk-io';

const vk = new VK({
    token: fs.readFileSync("token.txt", "utf8").trim(),
    apiLimit: 20,
    language: 'ru'
});

async function h_help(ctx, next) {
    if (/!help|!хелп/i.test(ctx.text)) {
        await ctx.send('Привет! Перешли мне любое одно сообщение и я сгенерирую из него цитату. На китайском и с эмодзи - без проблем 😉');
    } else {
        return next();
    }
}

async function compile_text(ctx_arr) {
    let text_lst = [];
    ctx_arr.forEach((msg) => {
        if (msg.hasText) text_lst.push(msg.text.replace(/\p{Extended_Pictographic}/u, '$&\uFE0F'));
    });
    return text_lst.join("\n");
}

async function h_fwd(ctx, next) {
    if (!ctx.hasForwards) return next();
    const fwd = ctx.forwards;
    const root_msg = fwd[0];
    const text = await compile_text(fwd);
    if (!text) {
        await ctx.send('Кажется, пересланные сообщения пустые. Мне нужен текст, чтобы получилась цитата 🧐');
        return;
    }
    if (text.length >= 10000) {
        await ctx.send('Какая большая цитата... Мне страшно её рисовать 😰');
        return;
    }
    let name, ava_link;
    if (root_msg.senderType === 'group') {
        const group = (await vk.api.groups.getById({
            group_ids: -root_msg.senderId
        }))[0];
        name = group.name;
        ava_link = group.photo_200;
    } else if (root_msg.senderType === 'user') {
        const user = (await vk.api.users.get({
            user_ids: root_msg.senderId,
            fields: 'photo_200'
        }))[0];
        name = user.first_name + ' ' + user.last_name;
        ava_link = user.photo_200;
    } else {
        console.log(root_msg);
        name = "Анонимус";
        ava_link = "https://vk.com/images/camera_200.png";
        await ctx.send("Я не понял от кого сообщение, сделаю анонимным. Передал сообщение разработчику на изучение 🖨");
    }
    const ava_buf = await (await fetch(ava_link)).arrayBuffer();
    try {
        await ctx.sendPhotos([{
            value: Buffer.from(cpp_back.gen_quote(text, name, ava_buf))
        }]);
    } catch (e) {
        console.log(e);
        await ctx.send("Что-то пошло не так. Пожалуйста, напиши сюда: https://vk.com/topic-211997710_48858993");
    }
}

async function h_fallback(ctx, next) {
    if (!ctx.isDM) return next();
    await ctx.send('Я не понял твоей команды, прости 😔\nОтправь "!хелп", чтобы получить список команд');
}

const msg_handlers = [h_help, h_fwd, h_fallback];
msg_handlers.forEach(f => {
    vk.updates.on('message_new', f);
});

vk.updates.start();