import bindings from 'bindings';
const cpp_back = bindings('newquote-cpp-backend');
import fs from 'fs';
import fetch from 'node-fetch';
import {stdin as input, stdout as output} from 'node:process';

const name = "В.И. Ленин";
// https://dev.vk.com/method/groups.getById
// https://dev.vk.com/method/users.get
const ava_link = "https://sun1-57.userapi.com/s/v1/if1/Qw3-M7tKCo0jE7k_YjNJHUypfjIR-VjxZwUl89BT-WrHGHd3l-AHzq1d4fIyTIG4ayrkg_pI.jpg?size=200x200&quality=96&crop=0,0,300,300&ava=1"
    || "https://vk.com/images/camera_200.png";
const text =
`Главная проблема цитат в Интернете заключается в том, что люди сразу верят в их подлинность ☝`;
const ava_buf = await (await fetch(ava_link)).arrayBuffer();

fs.writeFileSync('test.png', Buffer.from(cpp_back.gen_quote(text, name, ava_buf)));
console.log("Done, see test.png");
