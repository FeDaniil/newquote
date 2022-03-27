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

async function h_check_related(ctx, next) {
    if (ctx.isDM || (ctx.isChat && /club211997710/i.test(ctx.text))) {
        return next();
    }
}

async function h_help(ctx, next) {
    if (/!help|!хелп/i.test(ctx.text)) {
        await ctx.reply(
            ctx.isDM ? 'Привет! Перешли мне несколько сообщений и я сгенерирую из них цитату. На китайском и с эмодзи - без проблем 😉' :
                        'Перешли несколько сообщений и тегни @newquote, я сгенерирую из них цитату. На китайском и с эмодзи - без проблем 😉');
    } else {
        return next();
    }
}

async function compile_text(msg_ctx_arr) {
    let text_lst = [];
    msg_ctx_arr.forEach((msg) => {
        if (msg.hasText) text_lst.push(msg.text.replace(/\p{Extended_Pictographic}/u, '$&\uFE0F'));
    });
    return text_lst.join("\n");
}

async function get_text(msg_ctx, depth) {
    if (msg_ctx.hasForwards) {
        const fwd = msg_ctx.forwards;
        const root_ctx = fwd[0];
        if (!root_ctx.hasText) {
            return get_text(root_ctx, depth + 1);
        }
        return {
            text: await compile_text(fwd),
            root_ctx,
            depth
        };
    } else if (msg_ctx.hasReplyMessage) {
        const root_ctx = msg_ctx.replyMessage;
        if (!root_ctx.hasText) {
            return get_text(root_ctx);
        }
        return {
            text: root_ctx.text,
            root_ctx,
            depth
        }
    } else {
        return {
            text: null,
            root_ctx: msg_ctx,
            depth
        }
    }
}

async function get_ava_buf(ava_link) {
    return (await fetch(ava_link)).arrayBuffer();
}

async function get_group(group_id) {
    const group = (await vk.api.groups.getById({
        group_ids: [Math.abs(group_id)]
    }))[0];
    return {
        name: group.name,
        ava_buf: await get_ava_buf(group.photo_200)
    }
}

async function get_user(user_id) {
    const user = (await vk.api.users.get({
        user_ids: [user_id],
        fields: ['photo_200']
    }))[0];
    return {
        name: user.first_name + ' ' + user.last_name,
        ava_buf: await get_ava_buf(user.photo_200)
    };
}

async function get_author(root_ctx) {
    if (root_ctx.type === 'message') {
        if (root_ctx.senderType === 'group') {
            return get_group(-root_ctx.senderId);
        } else if (root_ctx.senderType === 'user') {
            return get_user(root_ctx.senderId);
        } else {
            console.log("Anon: ", root_ctx);
            return {
                name: "Анонимус",
                ava_buf: await get_ava_buf("https://vk.com/images/camera_200.png")
            };
        }
    } else {
        throw TypeError("context type not supported");
    }
}

async function override_author(text) {
    let iter = text.matchAll(/\[(id|club)(\d*)\|[^\]]*\]/gm);
    let match = iter.next().value;
    if (match && match[1] === 'club' && match[2] === '211997710') match = iter.next().value;
    if (match) {
        if (match[1] === 'id') {
            return get_user(parseInt(match[2]));
        } else if (match[1] === 'club') {
            return get_group(parseInt(match[2]));
        }
    }
    return null;
}

async function h_fwd(ctx, next) {
    const {text, root_ctx, depth} = await get_text(ctx, 0);
    if (text === null && depth === 0) {
        return next();
    }
    if (!text) {
        await ctx.reply('Кажется, сообщения для цитаты пустые или их нет. Мне нужен текст, чтобы получилась цитата 🧐');
        return;
    }
    if (text.length >= 10000) {
        await ctx.reply('Какая большая цитата... Мне страшно её рисовать 😰');
        return;
    }
    let author = ctx.text ? (await override_author(ctx.text)) : null;
    if (!author) {
        try {
            author = await get_author(root_ctx);
        } catch (e) {
            console.log(e);
            await ctx.reply('Я пока не поддерживаю этот тип автора');
            return;
        }
    }
    try {
        await ctx.send({
            attachment: await vk.upload.messagePhoto({
                source: {
                    value: Buffer.from(cpp_back.gen_quote(text, author.name, author.ava_buf))
                }
            }),
        });
    } catch (e) {
        console.log(e);
        await ctx.reply("Что-то пошло не так, попробуй ещё раз. Если не помогло, напиши сюда: https://vk.com/topic-211997710_48858993");
    }
}

async function h_dm_fallback(ctx, next) {
    if (!ctx.isDM) return next();
    await ctx.reply('Я не понял тебя, прости 😔\nОтправь "!хелп" и я скажу, что умею');
}

async function h_chat_fallback(ctx, next) {
    if (!ctx.isChat) return next();
    await ctx.reply('Я не понял тебя, прости 😔\nОтправь "@newquote !хелп" и я скажу, что умею');
}

const msg_handlers = [h_check_related, h_help, h_fwd, h_dm_fallback, h_chat_fallback];
msg_handlers.forEach(f => {
    vk.updates.on('message_new', f);
});

vk.updates.start().then(() => {
    console.log("started");
}, (err) => {
    console.log("FAILED to start!\n", err);
});